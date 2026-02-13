// David Eberly, Geometric Tools, Redmond WA 98052
// Copyright (c) 1998-2026
// Distributed under the Boost Software License, Version 1.0.
// https://www.boost.org/LICENSE_1_0.txt
// https://www.geometrictools.com/License/Boost/LICENSE_1_0.txt
// File Version: 8.0.2026.02.13
//
// Implementation of BallPivotMeshHoleFiller

#include <GTE/Mathematics/BallPivotMeshHoleFiller.h>
#include <iostream>
#include <queue>
#include <unordered_map>
#include <unordered_set>
#include <cmath>

namespace gte
{
    template <typename Real>
    bool BallPivotMeshHoleFiller<Real>::FillAllHoles(
        std::vector<Vector3<Real>>& vertices,
        std::vector<std::array<int32_t, 3>>& triangles,
        Parameters const& params)
    {
        if (vertices.empty() || triangles.empty())
        {
            return false;
        }
        
        if (params.verbose)
        {
            std::cout << "[BallPivotMeshHoleFiller] Starting hole detection and filling...\n";
            std::cout << "  Input: " << vertices.size() << " vertices, " << triangles.size() << " triangles\n";
        }
        
        int32_t iteration = 0;
        bool anyHolesFilled = false;
        
        while (iteration < params.maxIterations)
        {
            ++iteration;
            
            // Detect boundary loops (holes)
            auto holes = DetectBoundaryLoops(vertices, triangles);
            
            if (holes.empty())
            {
                if (params.verbose)
                {
                    std::cout << "  Iteration " << iteration << ": No holes found - mesh is closed!\n";
                }
                break;
            }
            
            if (params.verbose)
            {
                std::cout << "  Iteration " << iteration << ": Found " << holes.size() << " hole(s)\n";
            }
            
            bool holeFilledThisIteration = false;
            size_t initialTriangleCount = triangles.size();
            
            // Try to fill each hole
            for (size_t i = 0; i < holes.size(); ++i)
            {
                auto const& hole = holes[i];
                
                if (params.verbose)
                {
                    std::cout << "    Hole " << (i + 1) << ": " << hole.vertexIndices.size() 
                              << " boundary vertices, avg edge length = " << hole.avgEdgeLength << "\n";
                }
                
                if (FillHole(vertices, triangles, hole, params))
                {
                    holeFilledThisIteration = true;
                    anyHolesFilled = true;
                    
                    if (params.verbose)
                    {
                        std::cout << "      SUCCESS: Filled hole " << (i + 1) << "\n";
                    }
                }
                else
                {
                    if (params.verbose)
                    {
                        std::cout << "      FAILED: Could not fill hole " << (i + 1) << "\n";
                    }
                    
                    // Don't remove edge triangles - validation prevents non-manifold now
                    // This was causing more harm than good
                }
            }
            
            
            if (params.verbose)
            {
                if (triangles.size() > initialTriangleCount)
                {
                    size_t trianglesAdded = triangles.size() - initialTriangleCount;
                    std::cout << "  Iteration " << iteration << ": Added " << trianglesAdded << " triangle(s)\n";
                }
                else if (triangles.size() < initialTriangleCount)
                {
                    size_t trianglesRemoved = initialTriangleCount - triangles.size();
                    std::cout << "  Iteration " << iteration << ": Removed " << trianglesRemoved << " triangle(s)\n";
                }
            }
            
            // If no progress made, break
            if (!holeFilledThisIteration)
            {
                if (params.verbose)
                {
                    std::cout << "  No progress in iteration " << iteration << ", stopping.\n";
                }
                break;
            }
        }
        
        if (params.verbose)
        {
            auto finalHoles = DetectBoundaryLoops(vertices, triangles);
            std::cout << "[BallPivotMeshHoleFiller] Complete after " << iteration << " iteration(s)\n";
            std::cout << "  Output: " << vertices.size() << " vertices, " << triangles.size() << " triangles\n";
            std::cout << "  Remaining holes: " << finalHoles.size() << "\n";
        }
        
        return anyHolesFilled;
    }
    
    template <typename Real>
    bool BallPivotMeshHoleFiller<Real>::FillHole(
        std::vector<Vector3<Real>>& vertices,
        std::vector<std::array<int32_t, 3>>& triangles,
        BoundaryLoop const& hole,
        Parameters const& params)
    {
        if (hole.vertexIndices.size() < 3)
        {
            return false;  // Cannot fill a hole with fewer than 3 vertices
        }
        
        // If hole is just 3 vertices, directly add a triangle
        if (hole.vertexIndices.size() == 3)
        {
            triangles.push_back({hole.vertexIndices[0], hole.vertexIndices[1], hole.vertexIndices[2]});
            return true;
        }
        
        // Determine base radius from hole edge lengths
        Real baseRadius;
        switch (params.edgeMetric)
        {
            case EdgeMetric::Minimum:
                baseRadius = hole.minEdgeLength;
                break;
            case EdgeMetric::Maximum:
                baseRadius = hole.maxEdgeLength;
                break;
            case EdgeMetric::Average:
            default:
                baseRadius = hole.avgEdgeLength;
                break;
        }
        
        // Try progressively larger radii
        for (auto scale : params.radiusScales)
        {
            Real radius = baseRadius * scale;
            
            if (params.verbose)
            {
                std::cout << "      Trying radius = " << radius << " (base=" << baseRadius << ", scale=" << scale << ")\n";
            }
            
            if (FillHoleWithRadius(vertices, triangles, hole, radius, params))
            {
                return true;
            }
        }
        
        return false;  // All radii failed
    }
    
    template <typename Real>
    std::vector<typename BallPivotMeshHoleFiller<Real>::BoundaryLoop> 
    BallPivotMeshHoleFiller<Real>::DetectBoundaryLoops(
        std::vector<Vector3<Real>> const& vertices,
        std::vector<std::array<int32_t, 3>> const& triangles)
    {
        // Build vertex info with connectivity
        auto vertexInfo = BuildVertexInfo(vertices, triangles);
        
        // Find all boundary edges (edges with only one triangle)
        std::set<std::pair<int32_t, int32_t>> boundaryEdges;
        std::map<std::pair<int32_t, int32_t>, int32_t> edgeCount;
        
        for (auto const& tri : triangles)
        {
            for (int i = 0; i < 3; ++i)
            {
                int32_t v0 = tri[i];
                int32_t v1 = tri[(i + 1) % 3];
                auto edge = std::make_pair(std::min(v0, v1), std::max(v0, v1));
                edgeCount[edge]++;
            }
        }
        
        for (auto const& ec : edgeCount)
        {
            if (ec.second == 1)
            {
                boundaryEdges.insert(ec.first);
            }
        }
        
        if (boundaryEdges.empty())
        {
            return {};  // No holes
        }
        
        // Trace boundary loops
        std::vector<BoundaryLoop> loops;
        std::set<std::pair<int32_t, int32_t>> unvisitedEdges = boundaryEdges;
        
        while (!unvisitedEdges.empty())
        {
            auto startEdge = *unvisitedEdges.begin();
            auto loop = TraceBoundaryLoop(startEdge.first, unvisitedEdges, vertexInfo, vertices);
            
            if (!loop.vertexIndices.empty())
            {
                loops.push_back(loop);
            }
        }
        
        return loops;
    }
    
    template <typename Real>
    typename BallPivotMeshHoleFiller<Real>::BoundaryLoop 
    BallPivotMeshHoleFiller<Real>::TraceBoundaryLoop(
        int32_t startVertex,
        std::set<std::pair<int32_t, int32_t>>& unvisitedBoundaryEdges,
        std::vector<VertexInfo> const& vertexInfo,
        std::vector<Vector3<Real>> const& vertices)
    {
        BoundaryLoop loop;
        loop.vertexIndices.push_back(startVertex);
        
        int32_t currentVertex = startVertex;
        Real totalEdgeLength = static_cast<Real>(0);
        int32_t edgeCount = 0;
        
        // Follow the boundary
        bool foundNext = true;
        while (foundNext)
        {
            foundNext = false;
            
            // Find next boundary edge from current vertex
            for (auto it = unvisitedBoundaryEdges.begin(); it != unvisitedBoundaryEdges.end(); ++it)
            {
                auto const& edge = *it;
                int32_t nextVertex = -1;
                
                if (edge.first == currentVertex)
                {
                    nextVertex = edge.second;
                }
                else if (edge.second == currentVertex)
                {
                    nextVertex = edge.first;
                }
                
                if (nextVertex != -1)
                {
                    // Check if we've completed the loop
                    if (nextVertex == startVertex && loop.vertexIndices.size() > 2)
                    {
                        loop.isClosed = true;
                        unvisitedBoundaryEdges.erase(it);
                        foundNext = false;
                        break;
                    }
                    
                    // Add to loop
                    loop.vertexIndices.push_back(nextVertex);
                    
                    // Calculate edge length
                    Real edgeLength = Length(vertices[currentVertex] - vertices[nextVertex]);
                    totalEdgeLength += edgeLength;
                    loop.minEdgeLength = std::min(loop.minEdgeLength, edgeLength);
                    loop.maxEdgeLength = std::max(loop.maxEdgeLength, edgeLength);
                    ++edgeCount;
                    
                    unvisitedBoundaryEdges.erase(it);
                    currentVertex = nextVertex;
                    foundNext = true;
                    break;
                }
            }
        }
        
        // Calculate average edge length
        if (edgeCount > 0)
        {
            loop.avgEdgeLength = totalEdgeLength / static_cast<Real>(edgeCount);
        }
        
        return loop;
    }
    
    template <typename Real>
    std::vector<typename BallPivotMeshHoleFiller<Real>::VertexInfo> 
    BallPivotMeshHoleFiller<Real>::BuildVertexInfo(
        std::vector<Vector3<Real>> const& vertices,
        std::vector<std::array<int32_t, 3>> const& triangles)
    {
        std::vector<VertexInfo> vertexInfo(vertices.size());
        
        // Initialize positions
        for (size_t i = 0; i < vertices.size(); ++i)
        {
            vertexInfo[i].position = vertices[i];
        }
        
        // Build connectivity and normals
        for (size_t triIdx = 0; triIdx < triangles.size(); ++triIdx)
        {
            auto const& tri = triangles[triIdx];
            
            // Compute triangle normal
            auto const& v0 = vertices[tri[0]];
            auto const& v1 = vertices[tri[1]];
            auto const& v2 = vertices[tri[2]];
            
            Vector3<Real> normal = Cross(v1 - v0, v2 - v0);
            Real len = Length(normal);
            if (len > static_cast<Real>(0))
            {
                normal = normal / len;
            }
            
            // Add connectivity and accumulate normals
            for (int i = 0; i < 3; ++i)
            {
                int32_t vi = tri[i];
                int32_t vj = tri[(i + 1) % 3];
                int32_t vk = tri[(i + 2) % 3];
                
                vertexInfo[vi].connectedVertices.insert(vj);
                vertexInfo[vi].connectedVertices.insert(vk);
                vertexInfo[vi].connectedTriangles.insert(static_cast<int32_t>(triIdx));
                vertexInfo[vi].normal = vertexInfo[vi].normal + normal;
            }
        }
        
        // Normalize vertex normals
        for (auto& vi : vertexInfo)
        {
            Real len = Length(vi.normal);
            if (len > static_cast<Real>(0))
            {
                vi.normal = vi.normal / len;
            }
        }
        
        // Mark boundary vertices (check edge topology)
        std::map<std::pair<int32_t, int32_t>, int32_t> edgeCount;
        for (auto const& tri : triangles)
        {
            for (int i = 0; i < 3; ++i)
            {
                int32_t v0 = tri[i];
                int32_t v1 = tri[(i + 1) % 3];
                auto edge = std::make_pair(std::min(v0, v1), std::max(v0, v1));
                edgeCount[edge]++;
            }
        }
        
        for (auto const& ec : edgeCount)
        {
            if (ec.second == 1)  // Boundary edge
            {
                vertexInfo[ec.first.first].isBoundary = true;
                vertexInfo[ec.first.second].isBoundary = true;
            }
        }
        
        return vertexInfo;
    }
    
    template <typename Real>
    bool BallPivotMeshHoleFiller<Real>::FillHoleWithRadius(
        std::vector<Vector3<Real>>& vertices,
        std::vector<std::array<int32_t, 3>>& triangles,
        BoundaryLoop const& hole,
        Real radius,
        Parameters const& params)
    {
        // Skip degenerate holes (< 3 vertices)
        if (hole.vertexIndices.size() < 3)
        {
            return false;  // Cannot fill
        }
        
        // Build current edge topology to check for non-manifold creation
        std::map<std::pair<int32_t, int32_t>, int32_t> edgeCount;
        for (auto const& tri : triangles)
        {
            for (int i = 0; i < 3; ++i)
            {
                int32_t v0 = tri[i];
                int32_t v1 = tri[(i + 1) % 3];
                auto edge = std::make_pair(std::min(v0, v1), std::max(v0, v1));
                edgeCount[edge]++;
            }
        }
        
        // Helper function to check if adding a triangle would create non-manifold edges
        auto wouldCreateNonManifold = [&edgeCount](int32_t v0, int32_t v1, int32_t v2) -> bool
        {
            // Check all three edges
            for (int i = 0; i < 3; ++i)
            {
                int32_t a = (i == 0) ? v0 : (i == 1) ? v1 : v2;
                int32_t b = (i == 0) ? v1 : (i == 1) ? v2 : v0;
                auto edge = std::make_pair(std::min(a, b), std::max(a, b));
                
                // If edge already has 2 triangles, adding another would make it non-manifold
                if (edgeCount[edge] >= 2)
                {
                    return true;
                }
            }
            return false;
        };
        
        // Helper function to update edge counts after adding a triangle
        auto updateEdgeCounts = [&edgeCount](int32_t v0, int32_t v1, int32_t v2)
        {
            for (int i = 0; i < 3; ++i)
            {
                int32_t a = (i == 0) ? v0 : (i == 1) ? v1 : v2;
                int32_t b = (i == 0) ? v1 : (i == 1) ? v2 : v0;
                auto edge = std::make_pair(std::min(a, b), std::max(a, b));
                edgeCount[edge]++;
            }
        };
        
        // For simple holes with 3-4 vertices, use direct triangulation
        if (hole.vertexIndices.size() == 3)
        {
            int32_t v0 = hole.vertexIndices[0];
            int32_t v1 = hole.vertexIndices[1];
            int32_t v2 = hole.vertexIndices[2];
            
            if (!wouldCreateNonManifold(v0, v1, v2))
            {
                triangles.push_back({v0, v1, v2});
                return true;
            }
            return false;  // Would create non-manifold
        }
        else if (hole.vertexIndices.size() == 4)
        {
            int32_t v0 = hole.vertexIndices[0];
            int32_t v1 = hole.vertexIndices[1];
            int32_t v2 = hole.vertexIndices[2];
            int32_t v3 = hole.vertexIndices[3];
            
            // Check both triangles
            if (!wouldCreateNonManifold(v0, v1, v2) && !wouldCreateNonManifold(v0, v2, v3))
            {
                triangles.push_back({v0, v1, v2});
                updateEdgeCounts(v0, v1, v2);
                triangles.push_back({v0, v2, v3});
                return true;
            }
            return false;  // Would create non-manifold
        }
        
        // For more complex holes, use ear clipping algorithm with validation
        std::vector<int32_t> remainingVerts = hole.vertexIndices;
        size_t initialTriangleCount = triangles.size();
        
        // Keep removing ears until we have a triangle
        while (remainingVerts.size() > 3)
        {
            bool foundEar = false;
            
            for (size_t i = 0; i < remainingVerts.size(); ++i)
            {
                size_t prev = (i + remainingVerts.size() - 1) % remainingVerts.size();
                size_t next = (i + 1) % remainingVerts.size();
                
                int32_t v0 = remainingVerts[prev];
                int32_t v1 = remainingVerts[i];
                int32_t v2 = remainingVerts[next];
                
                // Check if this forms a valid ear
                Vector3<Real> const& p0 = vertices[v0];
                Vector3<Real> const& p1 = vertices[v1];
                Vector3<Real> const& p2 = vertices[v2];
                
                // Compute triangle normal and area
                Vector3<Real> edge1 = p1 - p0;
                Vector3<Real> edge2 = p2 - p0;
                Vector3<Real> triNormal = Cross(edge1, edge2);
                Real area = Length(triNormal);
                
                // Check if valid and wouldn't create non-manifold
                if (area > static_cast<Real>(1e-10) && !wouldCreateNonManifold(v0, v1, v2))
                {
                    triangles.push_back({v0, v1, v2});
                    updateEdgeCounts(v0, v1, v2);
                    
                    // Remove the ear vertex
                    remainingVerts.erase(remainingVerts.begin() + i);
                    foundEar = true;
                    break;
                }
            }
            
            if (!foundEar)
            {
                // Cannot find a valid ear - stop
                if (remainingVerts.size() == 3)
                {
                    break;
                }
                
                // Revert triangles added in this iteration
                triangles.resize(initialTriangleCount);
                return false;
            }
        }
        
        // Add final triangle if valid
        if (remainingVerts.size() == 3)
        {
            int32_t v0 = remainingVerts[0];
            int32_t v1 = remainingVerts[1];
            int32_t v2 = remainingVerts[2];
            
            if (!wouldCreateNonManifold(v0, v1, v2))
            {
                triangles.push_back({v0, v1, v2});
            }
            else
            {
                // Revert all triangles added in this hole fill
                triangles.resize(initialTriangleCount);
                return false;
            }
        }
        
        return triangles.size() > initialTriangleCount;
    }
    
    template <typename Real>
    int32_t BallPivotMeshHoleFiller<Real>::FindNextVertex(
        int32_t v0,
        int32_t v1,
        Real radius,
        std::vector<VertexInfo> const& vertexInfo,
        std::set<int32_t> const& availableVertices,
        Vector3<Real>& outBallCenter,
        Parameters const& params)
    {
        auto const& p0 = vertexInfo[v0].position;
        auto const& p1 = vertexInfo[v1].position;
        auto const& n0 = vertexInfo[v0].normal;
        auto const& n1 = vertexInfo[v1].normal;
        
        int32_t bestVertex = -1;
        Real minAngle = std::numeric_limits<Real>::max();
        Vector3<Real> bestCenter;
        
        // Try each available vertex
        for (int32_t v2 : availableVertices)
        {
            if (v2 == v0 || v2 == v1)
            {
                continue;
            }
            
            auto const& p2 = vertexInfo[v2].position;
            auto const& n2 = vertexInfo[v2].normal;
            
            // Check normal compatibility
            if (!AreNormalsCompatible(n0, n1, n2, p0, p1, p2, params.nsumMinDot))
            {
                continue;
            }
            
            // Compute ball center
            Vector3<Real> center;
            if (!ComputeBallCenter(p0, p1, p2, n0, n1, n2, radius, center))
            {
                continue;
            }
            
            // Check if ball is empty
            std::set<int32_t> excludeVertices = {v0, v1, v2};
            // Note: Ball emptiness check would require full vertex list
            // For efficiency and given we use ear clipping, we skip this check
            // If needed, pass vertexInfo positions to IsBallEmpty
            
            // Compute pivot angle (prefer smaller angles for tighter triangles)
            Vector3<Real> edge = p1 - p0;
            Vector3<Real> toCandidate = p2 - p0;
            Vector3<Real> normEdge = edge / Length(edge);
            Vector3<Real> normCandidate = toCandidate / Length(toCandidate);
            Real angle = std::acos(Dot(normEdge, normCandidate));
            
            if (angle < minAngle)
            {
                minAngle = angle;
                bestVertex = v2;
                bestCenter = center;
            }
        }
        
        if (bestVertex != -1)
        {
            outBallCenter = bestCenter;
        }
        
        return bestVertex;
    }
    
    template <typename Real>
    bool BallPivotMeshHoleFiller<Real>::ComputeBallCenter(
        Vector3<Real> const& p0,
        Vector3<Real> const& p1,
        Vector3<Real> const& p2,
        Vector3<Real> const& n0,
        Vector3<Real> const& n1,
        Vector3<Real> const& n2,
        Real radius,
        Vector3<Real>& center)
    {
        // Compute triangle normal
        Vector3<Real> edge1 = p1 - p0;
        Vector3<Real> edge2 = p2 - p0;
        Vector3<Real> triNormal = Cross(edge1, edge2);
        Real triNormalLen = Length(triNormal);
        
        if (triNormalLen < static_cast<Real>(1e-10))
        {
            return false;  // Degenerate triangle
        }
        
        triNormal = triNormal / triNormalLen;
        
        // Compute circumcenter in 2D (triangle plane)
        Real a = Length(edge1);
        Real b = Length(edge2);
        Real c = Length(p2 - p1);
        
        if (a < static_cast<Real>(1e-10) || b < static_cast<Real>(1e-10) || c < static_cast<Real>(1e-10))
        {
            return false;  // Degenerate edge
        }
        
        // Circumradius
        Real area = triNormalLen / static_cast<Real>(2);
        Real circumRadius = (a * b * c) / (static_cast<Real>(4) * area);
        
        if (circumRadius > radius)
        {
            return false;  // Ball too small for this triangle
        }
        
        // Circumcenter using barycentric coordinates
        Real d1 = Dot(edge1, edge1);
        Real d2 = Dot(edge2, edge2);
        Real d12 = Dot(edge1, edge2);
        
        Real denom = static_cast<Real>(2) * (d1 * d2 - d12 * d12);
        if (std::abs(denom) < static_cast<Real>(1e-10))
        {
            return false;
        }
        
        Real u = (d2 * d1 - d12 * d2) / denom;
        Real v = (d1 * d2 - d12 * d1) / denom;
        
        Vector3<Real> circumCenter = p0 + edge1 * u + edge2 * v;
        
        // Ball center is circumcenter offset along normal
        Real h2 = radius * radius - circumRadius * circumRadius;
        if (h2 < static_cast<Real>(0))
        {
            return false;
        }
        
        Real h = std::sqrt(h2);
        
        // Choose direction based on vertex normals
        Vector3<Real> sumNormal = n0 + n1 + n2;
        Real sumNormalLen = Length(sumNormal);
        
        if (sumNormalLen < static_cast<Real>(1e-10))
        {
            // Cannot determine direction from normals, use triangle normal
            center = circumCenter + triNormal * h;
            return true;
        }
        
        Vector3<Real> avgNormal = sumNormal / sumNormalLen;
        Real normalDot = Dot(avgNormal, triNormal);
        
        if (normalDot > static_cast<Real>(0))
        {
            center = circumCenter + triNormal * h;
        }
        else
        {
            center = circumCenter - triNormal * h;
        }
        
        return true;
    }
    
    template <typename Real>
    bool BallPivotMeshHoleFiller<Real>::AreNormalsCompatible(
        Vector3<Real> const& n0,
        Vector3<Real> const& n1,
        Vector3<Real> const& n2,
        Vector3<Real> const& p0,
        Vector3<Real> const& p1,
        Vector3<Real> const& p2,
        Real cosThreshold)
    {
        // Compute triangle normal
        Vector3<Real> triNormal = Cross(p1 - p0, p2 - p0);
        Real len = Length(triNormal);
        
        if (len < static_cast<Real>(1e-10))
        {
            return false;
        }
        
        triNormal = triNormal / len;
        
        // Check each vertex normal alignment with triangle normal
        Real dot0 = Dot(n0, triNormal);
        Real dot1 = Dot(n1, triNormal);
        Real dot2 = Dot(n2, triNormal);
        
        return dot0 >= cosThreshold && dot1 >= cosThreshold && dot2 >= cosThreshold;
    }
    
    template <typename Real>
    bool BallPivotMeshHoleFiller<Real>::IsBallEmpty(
        Vector3<Real> const& center,
        Real radius,
        std::vector<Vector3<Real>> const& vertices,
        std::set<int32_t> const& excludeVertices)
    {
        Real radius2 = radius * radius;
        
        for (size_t i = 0; i < vertices.size(); ++i)
        {
            if (excludeVertices.count(static_cast<int32_t>(i)))
            {
                continue;
            }
            
            Vector3<Real> diff = vertices[i] - center;
            Real dist2 = Dot(diff, diff);
            if (dist2 < radius2)
            {
                return false;  // Ball contains this vertex
            }
        }
        
        return true;
    }
    
    template <typename Real>
    std::vector<int32_t> BallPivotMeshHoleFiller<Real>::IdentifyEdgeTriangles(
        std::vector<Vector3<Real>> const& vertices,
        std::vector<std::array<int32_t, 3>> const& triangles,
        BoundaryLoop const& hole,
        Real threshold)
    {
        std::vector<int32_t> edgeTriangles;
        std::set<int32_t> boundaryVertices(hole.vertexIndices.begin(), hole.vertexIndices.end());
        
        // Find triangles with 2 or more boundary vertices
        for (size_t i = 0; i < triangles.size(); ++i)
        {
            auto const& tri = triangles[i];
            int32_t boundaryCount = 0;
            
            for (int j = 0; j < 3; ++j)
            {
                if (boundaryVertices.count(tri[j]))
                {
                    ++boundaryCount;
                }
            }
            
            // If triangle has 2+ boundary vertices, it's an edge triangle
            if (boundaryCount >= 2)
            {
                edgeTriangles.push_back(static_cast<int32_t>(i));
            }
        }
        
        return edgeTriangles;
    }
    
    template <typename Real>
    void BallPivotMeshHoleFiller<Real>::RemoveTriangles(
        std::vector<std::array<int32_t, 3>>& triangles,
        std::vector<int32_t> const& indicesToRemove)
    {
        // Sort indices in descending order to remove from back to front
        std::vector<int32_t> sortedIndices = indicesToRemove;
        std::sort(sortedIndices.begin(), sortedIndices.end(), std::greater<int32_t>());
        
        for (int32_t idx : sortedIndices)
        {
            if (idx >= 0 && idx < static_cast<int32_t>(triangles.size()))
            {
                triangles.erase(triangles.begin() + idx);
            }
        }
    }
    
    template <typename Real>
    Real BallPivotMeshHoleFiller<Real>::CalculateAdaptiveRadius(
        int32_t vertexIndex,
        std::vector<Vector3<Real>> const& vertices,
        std::vector<std::array<int32_t, 3>> const& triangles,
        EdgeMetric metric)
    {
        // Build vertex connectivity
        auto vertexInfo = BuildVertexInfo(vertices, triangles);
        
        if (vertexIndex < 0 || vertexIndex >= static_cast<int32_t>(vertexInfo.size()))
        {
            return static_cast<Real>(0);
        }
        
        auto const& vi = vertexInfo[vertexIndex];
        
        if (vi.connectedVertices.empty())
        {
            return static_cast<Real>(0);
        }
        
        // Calculate edge lengths
        std::vector<Real> edgeLengths;
        for (int32_t neighborIdx : vi.connectedVertices)
        {
            Real len = Length(vertices[neighborIdx] - vertices[vertexIndex]);
            edgeLengths.push_back(len);
        }
        
        // Return metric
        switch (metric)
        {
            case EdgeMetric::Minimum:
                return *std::min_element(edgeLengths.begin(), edgeLengths.end());
            
            case EdgeMetric::Maximum:
                return *std::max_element(edgeLengths.begin(), edgeLengths.end());
            
            case EdgeMetric::Average:
            default:
            {
                Real sum = static_cast<Real>(0);
                for (Real len : edgeLengths)
                {
                    sum += len;
                }
                return sum / static_cast<Real>(edgeLengths.size());
            }
        }
    }
    
    template <typename Real>
    std::vector<std::set<int32_t>> BallPivotMeshHoleFiller<Real>::DetectConnectedComponents(
        std::vector<std::array<int32_t, 3>> const& triangles)
    {
        // Build adjacency from triangles
        std::map<int32_t, std::set<int32_t>> adjacency;
        for (auto const& tri : triangles)
        {
            for (int i = 0; i < 3; ++i)
            {
                adjacency[tri[i]].insert(tri[(i + 1) % 3]);
                adjacency[tri[i]].insert(tri[(i + 2) % 3]);
            }
        }
        
        // DFS to find components
        std::vector<std::set<int32_t>> components;
        std::set<int32_t> visited;
        
        for (auto const& adj : adjacency)
        {
            int32_t start = adj.first;
            if (visited.count(start))
                continue;
            
            std::set<int32_t> component;
            std::vector<int32_t> stack = {start};
            
            while (!stack.empty())
            {
                int32_t v = stack.back();
                stack.pop_back();
                
                if (visited.count(v))
                    continue;
                
                visited.insert(v);
                component.insert(v);
                
                for (int32_t neighbor : adjacency[v])
                {
                    if (!visited.count(neighbor))
                    {
                        stack.push_back(neighbor);
                    }
                }
            }
            
            if (!component.empty())
            {
                components.push_back(component);
            }
        }
        
        return components;
    }
    
    template <typename Real>
    std::vector<std::pair<std::pair<int32_t, int32_t>, std::pair<int32_t, int32_t>>>
    BallPivotMeshHoleFiller<Real>::FindComponentGaps(
        std::vector<Vector3<Real>> const& vertices,
        std::vector<std::array<int32_t, 3>> const& triangles,
        std::vector<std::set<int32_t>> const& components,
        Real maxGapDistance)
    {
        std::vector<std::pair<std::pair<int32_t, int32_t>, std::pair<int32_t, int32_t>>> gaps;
        
        // Find boundary edges for each component
        std::map<std::pair<int32_t, int32_t>, int32_t> edgeCount;
        for (auto const& tri : triangles)
        {
            for (int i = 0; i < 3; ++i)
            {
                int32_t v0 = tri[i];
                int32_t v1 = tri[(i + 1) % 3];
                auto edge = std::make_pair(std::min(v0, v1), std::max(v0, v1));
                edgeCount[edge]++;
            }
        }
        
        std::vector<std::vector<std::pair<int32_t, int32_t>>> componentBoundaries(components.size());
        
        for (auto const& ec : edgeCount)
        {
            if (ec.second == 1)  // Boundary edge
            {
                // Find which component this edge belongs to
                for (size_t i = 0; i < components.size(); ++i)
                {
                    if (components[i].count(ec.first.first) && components[i].count(ec.first.second))
                    {
                        componentBoundaries[i].push_back(ec.first);
                        break;
                    }
                }
            }
        }
        
        // Find close boundary edges between different components
        // Skip components that have no boundary edges (they're closed)
        for (size_t i = 0; i < components.size(); ++i)
        {
            if (componentBoundaries[i].empty()) continue;  // Skip closed components
            
            for (size_t j = i + 1; j < components.size(); ++j)
            {
                if (componentBoundaries[j].empty()) continue;  // Skip closed components
                
                for (auto const& edge1 : componentBoundaries[i])
                {
                    Vector3<Real> mid1 = (vertices[edge1.first] + vertices[edge1.second]) / static_cast<Real>(2);
                    
                    for (auto const& edge2 : componentBoundaries[j])
                    {
                        Vector3<Real> mid2 = (vertices[edge2.first] + vertices[edge2.second]) / static_cast<Real>(2);
                        
                        Real dist = Length(mid2 - mid1);
                        if (dist < maxGapDistance)
                        {
                            gaps.push_back(std::make_pair(edge1, edge2));
                        }
                    }
                }
            }
        }
        
        return gaps;
    }
    
    template <typename Real>
    typename BallPivotMeshHoleFiller<Real>::BoundaryLoop
    BallPivotMeshHoleFiller<Real>::CreateVirtualBoundaryLoop(
        std::vector<Vector3<Real>> const& vertices,
        std::pair<int32_t, int32_t> const& edge1,
        std::pair<int32_t, int32_t> const& edge2)
    {
        BoundaryLoop loop;
        
        // Create a quadrilateral loop connecting the two edges
        // Order vertices to form a closed loop
        loop.vertexIndices = {edge1.first, edge1.second, edge2.second, edge2.first};
        loop.isClosed = true;
        
        // Calculate edge statistics
        Real totalLength = static_cast<Real>(0);
        for (size_t i = 0; i < 4; ++i)
        {
            int32_t v0 = loop.vertexIndices[i];
            int32_t v1 = loop.vertexIndices[(i + 1) % 4];
            Real len = Length(vertices[v1] - vertices[v0]);
            totalLength += len;
            loop.minEdgeLength = std::min(loop.minEdgeLength, len);
            loop.maxEdgeLength = std::max(loop.maxEdgeLength, len);
        }
        loop.avgEdgeLength = totalLength / static_cast<Real>(4);
        
        return loop;
    }
    
    template <typename Real>
    bool BallPivotMeshHoleFiller<Real>::FillAllHolesWithComponentBridging(
        std::vector<Vector3<Real>>& vertices,
        std::vector<std::array<int32_t, 3>>& triangles,
        Parameters const& params)
    {
        if (params.verbose)
        {
            std::cout << "\n[BallPivotMeshHoleFiller] Multi-Phase Component-Aware Hole Filling\n";
            std::cout << "========================================================\n";
        }
        
        bool anyProgress = false;
        int32_t iteration = 0;
        size_t initialTriangleCount = triangles.size();
        
        // Calculate average edge length for adaptive thresholds
        auto calculateAvgEdgeLength = [&]() -> Real
        {
            auto holes = DetectBoundaryLoops(vertices, triangles);
            Real avgEdgeLength = static_cast<Real>(0);
            int32_t edgeCount = 0;
            for (auto const& hole : holes)
            {
                if (hole.avgEdgeLength > static_cast<Real>(0))
                {
                    avgEdgeLength += hole.avgEdgeLength;
                    ++edgeCount;
                }
            }
            if (edgeCount > 0)
            {
                return avgEdgeLength / static_cast<Real>(edgeCount);
            }
            return static_cast<Real>(50.0);  // Default fallback
        };
        
        Real baseEdgeLength = calculateAvgEdgeLength();
        
        if (params.verbose)
        {
            std::cout << "Base edge length: " << baseEdgeLength << "\n";
        }
        
        while (iteration < params.maxIterations)
        {
            ++iteration;
            bool progressThisIteration = false;
            
            // Recalculate base edge length each iteration as mesh changes
            baseEdgeLength = calculateAvgEdgeLength();
            
            if (params.verbose)
            {
                std::cout << "\n--- Iteration " << iteration << " ---\n";
                std::cout << "Base edge length: " << baseEdgeLength << "\n";
            }
            
            // Step 1: Fill regular holes within components
            auto holes = DetectBoundaryLoops(vertices, triangles);
            
            if (params.verbose && !holes.empty())
            {
                std::cout << "Found " << holes.size() << " hole(s) within components\n";
            }
            
            size_t holesFilled = 0;
            size_t holeTrianglesBefore = triangles.size();
            
            for (auto const& hole : holes)
            {
                if (FillHole(vertices, triangles, hole, params))
                {
                    ++holesFilled;
                    progressThisIteration = true;
                    anyProgress = true;
                }
            }
            
            if (params.verbose && holesFilled > 0)
            {
                std::cout << "  Filled " << holesFilled << "/" << holes.size() << " hole(s), added " 
                          << (triangles.size() - holeTrianglesBefore) << " triangles\n";
            }
            
            // Step 2: Progressive component bridging (conservative to aggressive)
            auto components = DetectConnectedComponents(triangles);
            
            if (components.size() > 1)
            {
                if (params.verbose)
                {
                    std::cout << "  Components: " << components.size() << " (need bridging)\n";
                }
                
                // Progressive gap threshold scales: start small, increase gradually
                // Use very aggressive scales for sparse/distant components
                std::vector<Real> gapScales = {1.5, 2.0, 3.0, 5.0, 7.5, 10.0, 15.0, 20.0, 30.0, 50.0, 75.0, 100.0, 150.0, 200.0};
                
                // Debug: Show component boundary edge counts  
                if (params.verbose)
                {
                    std::cout << "    Component boundary edge distribution:\n";
                    std::map<std::pair<int32_t, int32_t>, int32_t> edgeCount;
                    for (auto const& tri : triangles)
                    {
                        for (int i = 0; i < 3; ++i)
                        {
                            int32_t v0 = tri[i];
                            int32_t v1 = tri[(i + 1) % 3];
                            auto edge = std::make_pair(std::min(v0, v1), std::max(v0, v1));
                            edgeCount[edge]++;
                        }
                    }
                    
                    std::vector<int32_t> boundaryEdgesPerComponent(components.size(), 0);
                    for (auto const& ec : edgeCount)
                    {
                        if (ec.second == 1)  // Boundary edge
                        {
                            for (size_t i = 0; i < components.size(); ++i)
                            {
                                if (components[i].count(ec.first.first) && components[i].count(ec.first.second))
                                {
                                    boundaryEdgesPerComponent[i]++;
                                    break;
                                }
                            }
                        }
                    }
                    
                    for (size_t i = 0; i < boundaryEdgesPerComponent.size(); ++i)
                    {
                        std::cout << "      Component " << i << ": " << components[i].size() 
                                  << " vertices, " << boundaryEdgesPerComponent[i] << " boundary edges"
                                  << (boundaryEdgesPerComponent[i] == 0 ? " (CLOSED)" : "") << "\n";
                    }
                }
                
                bool bridgedThisIteration = false;
                
                // Debug: Show component boundary edge counts
                if (params.verbose)
                {
                    std::map<std::pair<int32_t, int32_t>, int32_t> edgeCount;
                    for (auto const& tri : triangles)
                    {
                        for (int i = 0; i < 3; ++i)
                        {
                            int32_t v0 = tri[i];
                            int32_t v1 = tri[(i + 1) % 3];
                            auto edge = std::make_pair(std::min(v0, v1), std::max(v0, v1));
                            edgeCount[edge]++;
                        }
                    }
                    int32_t boundaryEdgeCount = 0;
                    for (auto const& ec : edgeCount)
                    {
                        if (ec.second == 1) ++boundaryEdgeCount;
                    }
                    std::cout << "    Total boundary edges: " << boundaryEdgeCount << "\n";
                }
                
                for (Real scale : gapScales)
                {
                    Real maxGapDist = baseEdgeLength * scale;
                    
                    if (params.verbose)
                    {
                        std::cout << "    Searching for gaps at threshold " << scale << "x (" << maxGapDist << ")...\n";
                    }
                    
                    auto gaps = FindComponentGaps(vertices, triangles, components, maxGapDist);
                    
                    if (gaps.empty())
                    {
                        if (params.verbose)
                        {
                            std::cout << "      No gaps found\n";
                        }
                        continue;  // Try next threshold
                    }
                    
                    if (params.verbose)
                    {
                        std::cout << "    Threshold " << scale << "x (" << maxGapDist 
                                  << "): found " << gaps.size() << " potential gap(s)\n";
                    }
                    
                    size_t gapsFilled = 0;
                    size_t gapTrianglesBefore = triangles.size();
                    
                    // Try to fill gaps at this threshold
                    for (auto const& gap : gaps)
                    {
                        auto virtualLoop = CreateVirtualBoundaryLoop(vertices, gap.first, gap.second);
                        if (FillHole(vertices, triangles, virtualLoop, params))
                        {
                            ++gapsFilled;
                            bridgedThisIteration = true;
                            progressThisIteration = true;
                            anyProgress = true;
                        }
                    }
                    
                    if (params.verbose && gapsFilled > 0)
                    {
                        std::cout << "      Bridged " << gapsFilled << " gap(s), added "
                                  << (triangles.size() - gapTrianglesBefore) << " triangles\n";
                    }
                    
                    // If we bridged any gaps, re-detect components and start over with small thresholds
                    if (gapsFilled > 0)
                    {
                        components = DetectConnectedComponents(triangles);
                        if (components.size() == 1)
                        {
                            if (params.verbose)
                            {
                                std::cout << "    Single component achieved!\n";
                            }
                            break;  // Exit gap bridging loop
                        }
                        else if (params.verbose)
                        {
                            std::cout << "    Components reduced to: " << components.size() << "\n";
                        }
                        break;  // Restart with conservative thresholds
                    }
                }
                
                if (!bridgedThisIteration && components.size() > 1)
                {
                    if (params.verbose)
                    {
                        std::cout << "    No gaps bridged at any threshold\n";
                    }
                }
            }
            else
            {
                if (params.verbose)
                {
                    std::cout << "  Single component achieved!\n";
                }
            }
            
            // Check if mesh is now closed
            auto remainingHoles = DetectBoundaryLoops(vertices, triangles);
            auto remainingComponents = DetectConnectedComponents(triangles);
            
            if (params.verbose)
            {
                std::cout << "  Iteration summary: " << remainingHoles.size() << " holes, " 
                          << remainingComponents.size() << " components, "
                          << triangles.size() << " triangles\n";
            }
            
            if (remainingHoles.empty() && remainingComponents.size() <= 1)
            {
                if (params.verbose)
                {
                    std::cout << "\n✓ Mesh is closed with single component!\n";
                }
                break;
            }
            
            if (!progressThisIteration)
            {
                if (params.verbose)
                {
                    std::cout << "\nNo progress in iteration " << iteration << ", stopping.\n";
                }
                break;
            }
        }
        
        // Final summary
        if (params.verbose)
        {
            auto finalHoles = DetectBoundaryLoops(vertices, triangles);
            auto finalComponents = DetectConnectedComponents(triangles);
            
            std::cout << "\n========================================================\n";
            std::cout << "[BallPivotMeshHoleFiller] Multi-Phase Complete\n";
            std::cout << "  Initial triangles: " << initialTriangleCount << "\n";
            std::cout << "  Final triangles: " << triangles.size() << "\n";
            std::cout << "  Triangles added: " << (triangles.size() - initialTriangleCount) << "\n";
            std::cout << "  Remaining holes: " << finalHoles.size() << "\n";
            std::cout << "  Connected components: " << finalComponents.size() << "\n";
            
            if (finalHoles.empty() && finalComponents.size() == 1)
            {
                std::cout << "  Status: ✓ CLOSED MANIFOLD (pending non-manifold check)\n";
            }
            else
            {
                std::cout << "  Status: ✗ Not yet closed\n";
            }
            std::cout << "========================================================\n";
        }
        
        return anyProgress;
    }
    
    // Explicit instantiations
    template class BallPivotMeshHoleFiller<float>;
    template class BallPivotMeshHoleFiller<double>;
}
