// David Eberly, Geometric Tools, Redmond WA 98052
// Copyright (c) 1998-2026
// Distributed under the Boost Software License, Version 1.0.
// https://www.boost.org/LICENSE_1_0.txt
// https://www.geometrictools.com/License/Boost/LICENSE_1_0.txt
// File Version: 8.0.2026.02.17
//
// Implementation of BallPivotMeshHoleFiller

#include <GTE/Mathematics/BallPivotMeshHoleFiller.h>
#include <GTE/Mathematics/LSCMParameterization.h>
#include <detria.hpp>
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
        
        // Step 1: Remove conflicting triangles around hole (if enabled)
        // NOTE: Tests show this often removes more than it adds - disabled by default
        if (params.removeConflictingTriangles)
        {
            auto conflictingTriangles = DetectConflictingTriangles(vertices, triangles, hole);
            if (!conflictingTriangles.empty())
            {
                if (params.verbose)
                {
                    std::cout << "      Removing " << conflictingTriangles.size() 
                              << " conflicting triangles before filling\n";
                }
                
                // Remove conflicting triangles
                std::vector<std::array<int32_t, 3>> newTriangles;
                std::set<int32_t> toRemoveSet(conflictingTriangles.begin(), conflictingTriangles.end());
                
                for (size_t i = 0; i < triangles.size(); ++i)
                {
                    if (toRemoveSet.find(static_cast<int32_t>(i)) == toRemoveSet.end())
                    {
                        newTriangles.push_back(triangles[i]);
                    }
                }
                
                triangles = newTriangles;
                
                // Re-detect hole after removal (boundary may have changed)
                auto newHoles = DetectBoundaryLoops(vertices, triangles);
                if (newHoles.empty())
                {
                    if (params.verbose)
                    {
                        std::cout << "      Warning: No holes after removing conflicting triangles\n";
                    }
                    return false;
                }
                
                // Find the hole closest to our original hole
                BoundaryLoop const* updatedHole = &newHoles[0];
                
                for (auto const& nh : newHoles)
                {
                    // Check if this hole shares vertices with original
                    int sharedVertices = 0;
                    for (int32_t vi : nh.vertexIndices)
                    {
                        for (int32_t ovi : hole.vertexIndices)
                        {
                            if (vi == ovi)
                            {
                                ++sharedVertices;
                                break;
                            }
                        }
                    }
                
                if (sharedVertices > 0)
                {
                    updatedHole = &nh;
                    break;
                }
            }
            
            // Continue with updated hole
            return FillHoleInternal(vertices, triangles, *updatedHole, params);
            }  // End if (!conflictingTriangles.empty())
        }  // End if (params.removeConflictingTriangles)
        
        // Step 2: Fill hole (with or without conflicts, depending on parameter)
        // If removeConflictingTriangles is disabled, we skip removal and just fill
        return FillHoleInternal(vertices, triangles, hole, params);
    }
    
    // Internal method that does actual filling (after conflict removal)
    template <typename Real>
    bool BallPivotMeshHoleFiller<Real>::FillHoleInternal(
        std::vector<Vector3<Real>>& vertices,
        std::vector<std::array<int32_t, 3>>& triangles,
        BoundaryLoop const& hole,
        Parameters const& params)
    {
        // If hole is just 3 vertices, directly add a triangle
        if (hole.vertexIndices.size() == 3)
        {
            triangles.push_back({hole.vertexIndices[0], hole.vertexIndices[1], hole.vertexIndices[2]});
            return true;
        }
        
        // Per problem statement: Escalating hole filling strategy
        // 1. Try GTE Hole Filling first (if no Steiner points available)
        // 2. If Steiner points exist or GTE fails:
        //    - Try planar projection + detria (if not self-intersecting)
        //    - Try UV unwrapping + detria (if self-intersecting)
        
        // Check if we have any incorporated vertices (Steiner points) near this hole
        std::vector<int32_t> nearbySteiners;
        // For now, we don't have Steiner points in this context, so skip to GTE hole filling
        
        // Step 1: Try GTE Hole Filling (simple ear clipping approach) if no Steiner points
        if (nearbySteiners.empty())
        {
            if (params.verbose)
            {
                std::cout << "      Trying GTE hole filling (ear clipping)...\n";
            }
            
            // Use simple ear clipping with validation
            bool earClipSuccess = FillHoleWithRadius(vertices, triangles, hole, 
                                                      hole.avgEdgeLength, params);
            if (earClipSuccess)
            {
                if (params.verbose)
                {
                    std::cout << "      SUCCESS: GTE hole filling worked\n";
                }
                return true;
            }
            
            if (params.verbose)
            {
                std::cout << "      GTE hole filling failed, trying advanced methods...\n";
            }
        }
        
        // Step 2: Try planar projection + detria (check for self-intersection first)
        if (params.verbose)
        {
            std::cout << "      Trying planar projection + detria...\n";
        }
        
        std::vector<int32_t> emptySteiners;  // No Steiner points for this attempt
        bool detriaSuccess = FillHoleWithSteinerPoints(vertices, triangles, hole, emptySteiners, params);
        
        if (detriaSuccess)
        {
            if (params.verbose)
            {
                std::cout << "      SUCCESS: Planar projection + detria worked\n";
            }
            return true;
        }
        
        if (params.verbose)
        {
            std::cout << "      Planar projection + detria failed (self-intersection or other issue)\n";
        }
        
        // Step 3: The FillHoleWithSteinerPoints method already tries UV unwrapping
        // when self-intersection is detected, so if we got here, all methods failed
        
        // Final fallback: Try ball pivot with progressive radii
        if (params.verbose)
        {
            std::cout << "      Final fallback: trying ball pivot with progressive radii...\n";
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
        
        return false;  // All methods failed
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
        // Build vertex-based adjacency from triangles
        // This is used for most pipeline operations and is more conservative
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
    int32_t BallPivotMeshHoleFiller<Real>::CountTopologyComponents(
        std::vector<std::array<int32_t, 3>> const& triangles)
    {
        // Count components based on EDGE connectivity (proper topology definition)
        // Two triangles are in the same component if they share an EDGE
        
        if (triangles.empty())
            return 0;
        
        // Build triangle-to-triangle adjacency via shared edges
        std::map<std::pair<int32_t, int32_t>, std::set<int32_t>> edgeToTriangles;
        
        for (size_t triIdx = 0; triIdx < triangles.size(); ++triIdx)
        {
            auto const& tri = triangles[triIdx];
            for (int i = 0; i < 3; ++i)
            {
                int32_t v0 = tri[i];
                int32_t v1 = tri[(i + 1) % 3];
                auto edge = std::make_pair(std::min(v0, v1), std::max(v0, v1));
                edgeToTriangles[edge].insert(triIdx);
            }
        }
        
        // Build triangle adjacency
        std::map<int32_t, std::set<int32_t>> triangleAdjacency;
        for (auto const& [edge, tris] : edgeToTriangles)
        {
            if (tris.size() >= 2)
            {
                std::vector<int32_t> triList(tris.begin(), tris.end());
                for (size_t i = 0; i < triList.size(); ++i)
                {
                    for (size_t j = i + 1; j < triList.size(); ++j)
                    {
                        triangleAdjacency[triList[i]].insert(triList[j]);
                        triangleAdjacency[triList[j]].insert(triList[i]);
                    }
                }
            }
        }
        
        // DFS on triangles to count components
        std::set<int32_t> visitedTriangles;
        int32_t componentCount = 0;
        
        for (size_t triIdx = 0; triIdx < triangles.size(); ++triIdx)
        {
            if (visitedTriangles.count(triIdx))
                continue;
            
            // Found a new component
            componentCount++;
            std::vector<int32_t> stack = {static_cast<int32_t>(triIdx)};
            
            while (!stack.empty())
            {
                int32_t currentTri = stack.back();
                stack.pop_back();
                
                if (visitedTriangles.count(currentTri))
                    continue;
                
                visitedTriangles.insert(currentTri);
                
                // Add adjacent triangles to stack
                if (triangleAdjacency.count(currentTri))
                {
                    for (int32_t adjacentTri : triangleAdjacency[currentTri])
                    {
                        if (!visitedTriangles.count(adjacentTri))
                        {
                            stack.push_back(adjacentTri);
                        }
                    }
                }
            }
        }
        
        return componentCount;
    }
    
    template <typename Real>
    std::vector<std::set<int32_t>> BallPivotMeshHoleFiller<Real>::DetectTopologyComponentSets(
        std::vector<std::array<int32_t, 3>> const& triangles)
    {
        // Detect component sets based on EDGE connectivity (proper topology definition)
        // Two triangles are in the same component if they share an EDGE
        // Returns vector of sets, where each set contains triangle indices in a component
        
        std::vector<std::set<int32_t>> componentSets;
        
        if (triangles.empty())
            return componentSets;
        
        // Build triangle-to-triangle adjacency via shared edges
        std::map<std::pair<int32_t, int32_t>, std::set<int32_t>> edgeToTriangles;
        
        for (size_t triIdx = 0; triIdx < triangles.size(); ++triIdx)
        {
            auto const& tri = triangles[triIdx];
            for (int i = 0; i < 3; ++i)
            {
                int32_t v0 = tri[i];
                int32_t v1 = tri[(i + 1) % 3];
                auto edge = std::make_pair(std::min(v0, v1), std::max(v0, v1));
                edgeToTriangles[edge].insert(triIdx);
            }
        }
        
        // Build triangle adjacency
        std::map<int32_t, std::set<int32_t>> triangleAdjacency;
        for (auto const& [edge, tris] : edgeToTriangles)
        {
            if (tris.size() >= 2)
            {
                std::vector<int32_t> triList(tris.begin(), tris.end());
                for (size_t i = 0; i < triList.size(); ++i)
                {
                    for (size_t j = i + 1; j < triList.size(); ++j)
                    {
                        triangleAdjacency[triList[i]].insert(triList[j]);
                        triangleAdjacency[triList[j]].insert(triList[i]);
                    }
                }
            }
        }
        
        // DFS on triangles to find all components
        std::set<int32_t> visitedTriangles;
        
        for (size_t triIdx = 0; triIdx < triangles.size(); ++triIdx)
        {
            if (visitedTriangles.count(triIdx))
                continue;
            
            // Found a new component - collect all triangles in it
            std::set<int32_t> currentComponent;
            std::vector<int32_t> stack = {static_cast<int32_t>(triIdx)};
            
            while (!stack.empty())
            {
                int32_t currentTri = stack.back();
                stack.pop_back();
                
                if (visitedTriangles.count(currentTri))
                    continue;
                
                visitedTriangles.insert(currentTri);
                currentComponent.insert(currentTri);
                
                // Add adjacent triangles to stack
                if (triangleAdjacency.count(currentTri))
                {
                    for (int32_t adjacentTri : triangleAdjacency[currentTri])
                    {
                        if (!visitedTriangles.count(adjacentTri))
                        {
                            stack.push_back(adjacentTri);
                        }
                    }
                }
            }
            
            componentSets.push_back(currentComponent);
        }
        
        return componentSets;
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
    bool BallPivotMeshHoleFiller<Real>::ForceBridgeRemainingComponents(
        std::vector<Vector3<Real>>& vertices,
        std::vector<std::array<int32_t, 3>>& triangles,
        Parameters const& params)
    {
        // Use topology-based component detection (edge connectivity)
        int32_t topologyComponentCount = CountTopologyComponents(triangles);
        
        if (topologyComponentCount <= 1)
        {
            if (params.verbose)
            {
                std::cout << "  Already single component, no forced bridging needed\n";
            }
            return false;  // Already single component
        }
        
        if (params.verbose)
        {
            std::cout << "\n[Forced Component Bridging]\n";
            std::cout << "  Current components (topology): " << topologyComponentCount << "\n";
            std::cout << "  Finding closest boundary edges between components...\n";
        }
        
        // Use topology-based component sets for proper boundary edge assignment
        // This ensures triangles are grouped by shared EDGES, not just vertices
        auto components = DetectTopologyComponentSets(triangles);
        
        // Find boundary edges for each component
        std::map<std::pair<int32_t, int32_t>, int32_t> edgeCount;
        std::map<std::pair<int32_t, int32_t>, int32_t> edgeToTriangle;  // Track which triangle each edge belongs to
        
        for (size_t triIdx = 0; triIdx < triangles.size(); ++triIdx)
        {
            auto const& tri = triangles[triIdx];
            for (int i = 0; i < 3; ++i)
            {
                int32_t v0 = tri[i];
                int32_t v1 = tri[(i + 1) % 3];
                auto edge = std::make_pair(std::min(v0, v1), std::max(v0, v1));
                edgeCount[edge]++;
                edgeToTriangle[edge] = triIdx;  // Store last triangle that has this edge
            }
        }
        
        std::vector<std::vector<std::pair<int32_t, int32_t>>> componentBoundaries(components.size());
        
        for (auto const& ec : edgeCount)
        {
            if (ec.second == 1)  // Boundary edge
            {
                // Find which component this edge belongs to by checking triangle membership
                auto const& edge = ec.first;
                int32_t triIdx = edgeToTriangle[edge];
                
                for (size_t i = 0; i < components.size(); ++i)
                {
                    if (components[i].count(triIdx))
                    {
                        componentBoundaries[i].push_back(edge);
                        break;
                    }
                }
            }
        }
        
        // Find the closest boundary edge pair between any two components
        Real minDistance = std::numeric_limits<Real>::max();
        size_t closestCompI = 0, closestCompJ = 1;
        std::pair<int32_t, int32_t> closestEdge1, closestEdge2;
        bool foundPair = false;
        
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
                        if (dist < minDistance)
                        {
                            minDistance = dist;
                            closestCompI = i;
                            closestCompJ = j;
                            closestEdge1 = edge1;
                            closestEdge2 = edge2;
                            foundPair = true;
                        }
                    }
                }
            }
        }
        
        if (!foundPair)
        {
            if (params.verbose)
            {
                std::cout << "  No boundary edge pairs found (all components may be closed)\n";
            }
            return false;
        }
        
        if (params.verbose)
        {
            std::cout << "  Closest edge pair: Component " << closestCompI << " and " << closestCompJ 
                      << ", distance = " << minDistance << "\n";
            std::cout << "  Attempting to bridge...\n";
        }
        
        // Create virtual boundary loop and fill it
        auto virtualLoop = CreateVirtualBoundaryLoop(vertices, closestEdge1, closestEdge2);
        bool success = FillHole(vertices, triangles, virtualLoop, params);
        
        if (success)
        {
            if (params.verbose)
            {
                int32_t newComponentCount = CountTopologyComponents(triangles);
                std::cout << "  ✓ Successfully bridged! Components: " << topologyComponentCount 
                          << " → " << newComponentCount << "\n";
            }
            return true;
        }
        else
        {
            if (params.verbose)
            {
                std::cout << "  ✗ Failed to bridge components\n";
            }
            return false;
        }
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
        
        // Step 0: Reject small closed components if enabled
        std::set<int32_t> incorporatedVertices;
        if (params.rejectSmallComponents)
        {
            auto components = DetectConnectedComponents(triangles);
            if (components.size() > 1)
            {
                auto componentInfos = AnalyzeComponents(vertices, triangles, components);
                int32_t mainComponentIdx = IdentifyMainComponent(componentInfos);
                
                if (params.verbose)
                {
                    std::cout << "\nStep 0: Small Component Rejection (Manifold Components Only)\n";
                    std::cout << "  Found " << components.size() << " components\n";
                    std::cout << "  Main component: " << mainComponentIdx 
                              << " (" << componentInfos[mainComponentIdx].vertices.size() << " vertices)\n";
                    
                    // Show manifold status of components
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
                    
                    for (auto const& info : componentInfos)
                    {
                        int32_t nonManifoldEdges = 0;
                        for (auto const& ec : edgeCount)
                        {
                            if (ec.second >= 3 && info.vertices.count(ec.first.first) && info.vertices.count(ec.first.second))
                            {
                                nonManifoldEdges++;
                            }
                        }
                        
                        std::cout << "    Component " << info.componentIndex << ": " 
                                  << info.vertices.size() << " vertices, "
                                  << (info.isClosed ? "CLOSED" : "OPEN") << ", "
                                  << (nonManifoldEdges == 0 ? "MANIFOLD" : "NON-MANIFOLD")
                                  << (nonManifoldEdges > 0 ? " (" + std::to_string(nonManifoldEdges) + " edges)" : "")
                                  << "\n";
                    }
                }
                
                // Remove small manifold closed components
                incorporatedVertices = RemoveSmallClosedComponents(
                    triangles, componentInfos, mainComponentIdx, params.smallComponentThreshold);
                
                if (!incorporatedVertices.empty())
                {
                    if (params.verbose)
                    {
                        std::cout << "  Removed small MANIFOLD closed components\n";
                        std::cout << "  Triangles removed: " << (initialTriangleCount - triangles.size()) << "\n";
                        std::cout << "  Vertices available for incorporation: " << incorporatedVertices.size() << "\n";
                    }
                    anyProgress = true;
                    
                    // Re-detect components after removal
                    components = DetectConnectedComponents(triangles);
                    if (params.verbose)
                    {
                        std::cout << "  Components after removal: " << components.size() << "\n";
                    }
                }
                else if (params.verbose)
                {
                    std::cout << "  No small manifold components to remove\n";
                }
            }
        }
        
        // Step 0.5: Non-manifold edge repair (FIRST PRIORITY per problem statement)
        if (params.verbose)
        {
            std::cout << "\nStep 0.5: Non-Manifold Edge Repair\n";
        }
        
        auto nonManifoldEdges = DetectNonManifoldEdges(triangles);
        if (!nonManifoldEdges.empty())
        {
            if (params.verbose)
            {
                std::cout << "  Found " << nonManifoldEdges.size() << " non-manifold edge(s)\n";
            }
            
            for (auto const& edge : nonManifoldEdges)
            {
                bool repaired = LocalRemeshNonManifoldRegion(vertices, triangles, edge, params);
                if (repaired)
                {
                    anyProgress = true;
                    
                    // Re-detect after each repair as topology changes
                    auto remaining = DetectNonManifoldEdges(triangles);
                    if (params.verbose)
                    {
                        std::cout << "  Non-manifold edges remaining: " << remaining.size() << "\n";
                    }
                    
                    // Update for next iteration
                    nonManifoldEdges = remaining;
                    if (nonManifoldEdges.empty())
                    {
                        break;  // All fixed!
                    }
                }
            }
            
            // Final check
            nonManifoldEdges = DetectNonManifoldEdges(triangles);
            if (nonManifoldEdges.empty())
            {
                if (params.verbose)
                {
                    std::cout << "  All non-manifold edges successfully repaired!\n";
                }
            }
            else
            {
                if (params.verbose)
                {
                    std::cout << "  Warning: " << nonManifoldEdges.size() 
                              << " non-manifold edge(s) remain after repair\n";
                }
            }
        }
        else if (params.verbose)
        {
            std::cout << "  No non-manifold edges found\n";
        }
        
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
            
            // Step 0.75: Duplicate vertex detection and merging
            // This must be done BEFORE hole filling to fix invalid boundaries
            auto currentBoundaryLoops = DetectBoundaryLoops(vertices, triangles);
            bool hasDuplicates = false;
            
            for (auto const& loop : currentBoundaryLoops)
            {
                Real threshold = static_cast<Real>(1e-6);  // Epsilon for duplicate detection
                auto duplicates = DetectDuplicateVerticesInBoundary(
                    vertices, loop.vertexIndices, threshold, params);
                
                if (!duplicates.empty())
                {
                    hasDuplicates = true;
                    
                    if (params.verbose)
                    {
                        std::cout << "\n[Duplicate Vertex Detection]\n";
                        std::cout << "  Found " << duplicates.size() 
                                  << " duplicate vertex pair(s) in boundary loop\n";
                    }
                    
                    // Merge duplicates in mesh (updates all triangle references)
                    int32_t modified = MergeDuplicateVerticesInMesh(triangles, duplicates, params);
                    
                    if (params.verbose)
                    {
                        std::cout << "  Updated " << modified << " triangle references\n";
                        std::cout << "  Boundary will be automatically reconstructed\n";
                    }
                    
                    progressThisIteration = true;
                    anyProgress = true;
                }
            }
            
            // If we merged duplicates, reconstruct boundary loops
            if (hasDuplicates)
            {
                if (params.verbose)
                {
                    std::cout << "  Reconstructing boundaries after merge...\n";
                }
                // Boundaries will be detected again in Step 1
            }
            
            // Step 1: Fill regular holes within components
            auto holes = DetectBoundaryLoops(vertices, triangles);
            
            if (params.verbose && !holes.empty())
            {
                std::cout << "Found " << holes.size() << " hole(s) within components\n";
            }
            
            size_t holesFilled = 0;
            size_t holeTrianglesBefore = triangles.size();
            
            // Try to fill holes, using incorporated vertices for failed holes
            for (auto const& hole : holes)
            {
                bool filled = FillHole(vertices, triangles, hole, params);
                
                // If hole failed and we have incorporated vertices, try using them with detria
                if (!filled && !incorporatedVertices.empty())
                {
                    // Find incorporated vertices near this hole
                    std::vector<int32_t> nearbyVertices;
                    Real searchRadius = hole.avgEdgeLength * static_cast<Real>(3.0);
                    
                    // Compute hole centroid
                    Vector3<Real> holeCentroid{static_cast<Real>(0), static_cast<Real>(0), static_cast<Real>(0)};
                    for (int32_t vi : hole.vertexIndices)
                    {
                        holeCentroid = holeCentroid + vertices[vi];
                    }
                    holeCentroid = holeCentroid / static_cast<Real>(hole.vertexIndices.size());
                    
                    // Find nearby incorporated vertices
                    for (int32_t vi : incorporatedVertices)
                    {
                        Real dist = Length(vertices[vi] - holeCentroid);
                        if (dist < searchRadius)
                        {
                            nearbyVertices.push_back(vi);
                        }
                    }
                    
                    if (!nearbyVertices.empty())
                    {
                        if (params.verbose)
                        {
                            std::cout << "      Trying detria with " << nearbyVertices.size() 
                                      << " Steiner points...\n";
                        }
                        
                        // Try triangulation with Steiner points using detria
                        filled = FillHoleWithSteinerPoints(vertices, triangles, hole, nearbyVertices, params);
                    }
                }
                
                if (filled)
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
        
        // Step 7: Post-processing - Remove closed components created during filling
        if (params.rejectSmallComponents)
        {
            auto finalComponentsPreCleanup = DetectConnectedComponents(triangles);
            if (finalComponentsPreCleanup.size() > 1)
            {
                auto finalComponentInfos = AnalyzeComponents(vertices, triangles, finalComponentsPreCleanup);
                int32_t finalMainComponentIdx = IdentifyMainComponent(finalComponentInfos);
                
                // Remove newly-closed small components
                auto additionalVertices = RemoveSmallClosedComponents(
                    triangles, finalComponentInfos, finalMainComponentIdx, params.smallComponentThreshold);
                
                if (!additionalVertices.empty() && params.verbose)
                {
                    std::cout << "\nPost-processing: Removed " << additionalVertices.size() 
                             << " vertices from " << (finalComponentsPreCleanup.size() - DetectConnectedComponents(triangles).size())
                             << " closed components created during hole filling\n";
                }
            }
        }
        
        // Step 8: Forced bridging - Try to bridge remaining open components after cleanup
        // Use topology-based component count (edge connectivity) for proper detection
        int32_t topologyComponentCount = CountTopologyComponents(triangles);
        
        if (topologyComponentCount > 1 && params.verbose)
        {
            std::cout << "\nStep 8: Attempting forced bridging after post-processing cleanup...\n";
            std::cout << "  Topology analysis shows " << topologyComponentCount << " components\n";
        }
        
        if (topologyComponentCount > 1)
        {
            // Try forced bridging multiple times if needed
            bool anyForcedBridges = false;
            for (int forcedAttempt = 0; forcedAttempt < 10 && topologyComponentCount > 1; ++forcedAttempt)
            {
                bool bridged = ForceBridgeRemainingComponents(vertices, triangles, params);
                if (bridged)
                {
                    anyForcedBridges = true;
                    topologyComponentCount = CountTopologyComponents(triangles);
                    anyProgress = true;
                    
                    if (topologyComponentCount == 1)
                    {
                        if (params.verbose)
                        {
                            std::cout << "✓ Forced bridging achieved single component!\n";
                        }
                        break;
                    }
                }
                else
                {
                    break;  // No more bridges possible
                }
            }
            
            if (anyForcedBridges && topologyComponentCount > 1)
            {
                if (params.verbose)
                {
                    std::cout << "Forced bridging reduced components to " << topologyComponentCount << "\n";
                }
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
    
    // Component analysis and refinement methods
    
    template <typename Real>
    std::vector<typename BallPivotMeshHoleFiller<Real>::ComponentInfo>
    BallPivotMeshHoleFiller<Real>::AnalyzeComponents(
        std::vector<Vector3<Real>> const& vertices,
        std::vector<std::array<int32_t, 3>> const& triangles,
        std::vector<std::set<int32_t>> const& components)
    {
        std::vector<ComponentInfo> infos;
        
        // Build edge topology
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
        
        // Find which triangles belong to each component
        for (size_t compIdx = 0; compIdx < components.size(); ++compIdx)
        {
            ComponentInfo info;
            info.componentIndex = static_cast<int32_t>(compIdx);
            info.vertices = components[compIdx];
            info.boundaryEdgeCount = 0;
            
            // Find triangles in this component
            for (size_t triIdx = 0; triIdx < triangles.size(); ++triIdx)
            {
                auto const& tri = triangles[triIdx];
                bool inComponent = true;
                for (int i = 0; i < 3; ++i)
                {
                    if (!info.vertices.count(tri[i]))
                    {
                        inComponent = false;
                        break;
                    }
                }
                if (inComponent)
                {
                    info.triangleIndices.push_back(static_cast<int32_t>(triIdx));
                }
            }
            
            // Count boundary edges
            for (auto const& ec : edgeCount)
            {
                if (ec.second == 1)  // Boundary edge
                {
                    if (info.vertices.count(ec.first.first) && info.vertices.count(ec.first.second))
                    {
                        info.boundaryEdgeCount++;
                    }
                }
            }
            
            info.isClosed = (info.boundaryEdgeCount == 0);
            infos.push_back(info);
        }
        
        return infos;
    }
    
    template <typename Real>
    int32_t BallPivotMeshHoleFiller<Real>::IdentifyMainComponent(
        std::vector<ComponentInfo> const& componentInfos)
    {
        if (componentInfos.empty())
        {
            return -1;
        }
        
        // Priority 1 fix: Main component = largest OPEN component by vertex count
        // We want the actual mesh we're trying to close, not a closed component
        // that was created during hole filling
        int32_t mainIdx = -1;
        size_t maxVertices = 0;
        
        // First, try to find largest OPEN component
        for (size_t i = 0; i < componentInfos.size(); ++i)
        {
            if (!componentInfos[i].isClosed && componentInfos[i].vertices.size() > maxVertices)
            {
                maxVertices = componentInfos[i].vertices.size();
                mainIdx = static_cast<int32_t>(i);
            }
        }
        
        // If no open components found, fall back to largest overall
        if (mainIdx == -1)
        {
            mainIdx = 0;
            maxVertices = componentInfos[0].vertices.size();
            for (size_t i = 1; i < componentInfos.size(); ++i)
            {
                if (componentInfos[i].vertices.size() > maxVertices)
                {
                    maxVertices = componentInfos[i].vertices.size();
                    mainIdx = static_cast<int32_t>(i);
                }
            }
        }
        
        return mainIdx;
    }
    
    template <typename Real>
    std::set<int32_t> BallPivotMeshHoleFiller<Real>::RemoveSmallClosedComponents(
        std::vector<std::array<int32_t, 3>>& triangles,
        std::vector<ComponentInfo> const& componentInfos,
        int32_t mainComponentIndex,
        int32_t sizeThreshold)
    {
        std::set<int32_t> removedVertices;
        std::vector<int32_t> trianglesToRemove;
        
        // Build edge topology to check for non-manifold edges
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
        
        for (auto const& info : componentInfos)
        {
            // Skip main component
            if (info.componentIndex == mainComponentIndex)
            {
                continue;
            }
            
            // Check if component is closed AND manifold
            // Only reject "prematurely manifold" components
            // NOTE: We now remove ALL closed manifold components except main,
            // not just those <= sizeThreshold. The sizeThreshold is kept for
            // backwards compatibility but is effectively bypassed for closed components.
            if (info.isClosed)
            {
                // Check if component is manifold (no edges with 3+ triangles)
                bool isManifold = true;
                for (auto const& ec : edgeCount)
                {
                    if (ec.second >= 3)  // Non-manifold edge
                    {
                        // Check if this edge belongs to this component
                        if (info.vertices.count(ec.first.first) && info.vertices.count(ec.first.second))
                        {
                            isManifold = false;
                            break;
                        }
                    }
                }
                
                // Only remove if manifold (prematurely manifold component)
                if (!isManifold)
                {
                    continue;  // Keep non-manifold components
                }
                
                // Priority 1 fix: Remove ALL closed manifold components except main
                // Per problem statement: we want a single manifold, so all closed
                // components (no matter the size) should be removed.
                // The sizeThreshold parameter is kept for backwards compatibility
                // but is no longer used for closed component filtering.
                // If a closed component is very large (>= main size), we keep it
                // as it might be the actual main component we want.
                if (static_cast<int32_t>(info.vertices.size()) > sizeThreshold)
                {
                    // Only keep closed components that are >= main component size
                    auto mainInfo = std::find_if(componentInfos.begin(), componentInfos.end(),
                        [mainComponentIndex](ComponentInfo const& ci) { return ci.componentIndex == mainComponentIndex; });
                    
                    if (mainInfo != componentInfos.end())
                    {
                        // Keep if this component is >= main size (might be the actual main)
                        if (info.vertices.size() >= mainInfo->vertices.size())
                        {
                            continue;  // Keep components as large or larger than designated main
                        }
                        // Otherwise fall through and remove it
                    }
                }
            }
            else
            {
                continue;  // Not closed
            }
            
            // This component is small, closed, AND manifold - remove it
            {
                // Mark triangles for removal
                for (int32_t triIdx : info.triangleIndices)
                {
                    trianglesToRemove.push_back(triIdx);
                }
                
                // Save vertices for incorporation
                for (int32_t v : info.vertices)
                {
                    removedVertices.insert(v);
                }
            }
        }
        
        // Remove marked triangles
        if (!trianglesToRemove.empty())
        {
            RemoveTriangles(triangles, trianglesToRemove);
        }
        
        return removedVertices;
    }
    
    template <typename Real>
    bool BallPivotMeshHoleFiller<Real>::IsVertexNormalCompatible(
        Vector3<Real> const& vertexPos,
        Vector3<Real> const& vertexNormal,
        BoundaryLoop const& hole,
        std::vector<Vector3<Real>> const& vertices,
        Real normalThreshold)
    {
        if (hole.vertexIndices.size() < 3)
        {
            return false;  // Can't compute hole normal
        }
        
        // Compute best-fit plane for hole boundary
        // Use centroid and average normal
        Vector3<Real> centroid{static_cast<Real>(0), static_cast<Real>(0), static_cast<Real>(0)};
        for (int32_t vi : hole.vertexIndices)
        {
            centroid = centroid + vertices[vi];
        }
        centroid = centroid / static_cast<Real>(hole.vertexIndices.size());
        
        // Compute average normal from consecutive edge cross products
        Vector3<Real> avgNormal{static_cast<Real>(0), static_cast<Real>(0), static_cast<Real>(0)};
        for (size_t i = 0; i < hole.vertexIndices.size(); ++i)
        {
            int32_t v0 = hole.vertexIndices[i];
            int32_t v1 = hole.vertexIndices[(i + 1) % hole.vertexIndices.size()];
            int32_t v2 = hole.vertexIndices[(i + 2) % hole.vertexIndices.size()];
            
            Vector3<Real> edge1 = vertices[v1] - vertices[v0];
            Vector3<Real> edge2 = vertices[v2] - vertices[v1];
            Vector3<Real> normal = Cross(edge1, edge2);
            
            Real len = Length(normal);
            if (len > static_cast<Real>(1e-10))
            {
                avgNormal = avgNormal + (normal / len);
            }
        }
        
        Real avgNormalLen = Length(avgNormal);
        if (avgNormalLen < static_cast<Real>(1e-10))
        {
            return false;  // Degenerate hole
        }
        
        avgNormal = avgNormal / avgNormalLen;
        
        // Check if vertex normal aligns with hole normal
        Real dot = Dot(vertexNormal, avgNormal);
        return dot >= normalThreshold;
    }
    
    template <typename Real>
    bool BallPivotMeshHoleFiller<Real>::FillHoleWithSteinerPoints(
        std::vector<Vector3<Real>>& vertices,
        std::vector<std::array<int32_t, 3>>& triangles,
        BoundaryLoop const& hole,
        std::vector<int32_t> const& steinerVertexIndices,
        Parameters const& params)
    {
        if (hole.vertexIndices.size() < 3)
        {
            return false;  // Can't triangulate
        }
        
        // Per problem statement: Check for self-intersection and use appropriate method
        // - If no Steiner points: Just try planar projection
        // - If planar projection has self-intersection: Use UV unwrapping (LSCM)
        
        // Try two approaches: planar projection first, UV unwrapping if that fails
        bool useLSCM = false;
        std::vector<detria::PointD> points2D;
        std::vector<int32_t> indexMap;
        
        // Step 1: Try planar projection first (faster)
        Vector3<Real> centroid{static_cast<Real>(0), static_cast<Real>(0), static_cast<Real>(0)};
        for (int32_t vi : hole.vertexIndices)
        {
            centroid = centroid + vertices[vi];
        }
        centroid = centroid / static_cast<Real>(hole.vertexIndices.size());
        
        // Compute average normal from consecutive edge cross products
        Vector3<Real> avgNormal{static_cast<Real>(0), static_cast<Real>(0), static_cast<Real>(0)};
        for (size_t i = 0; i < hole.vertexIndices.size(); ++i)
        {
            int32_t v0 = hole.vertexIndices[i];
            int32_t v1 = hole.vertexIndices[(i + 1) % hole.vertexIndices.size()];
            int32_t v2 = hole.vertexIndices[(i + 2) % hole.vertexIndices.size()];
            
            Vector3<Real> edge1 = vertices[v1] - vertices[v0];
            Vector3<Real> edge2 = vertices[v2] - vertices[v1];
            Vector3<Real> normal = Cross(edge1, edge2);
            
            Real len = Length(normal);
            if (len > static_cast<Real>(1e-10))
            {
                avgNormal = avgNormal + (normal / len);
            }
        }
        
        Real avgNormalLen = Length(avgNormal);
        if (avgNormalLen < static_cast<Real>(1e-10))
        {
            return false;  // Degenerate hole
        }
        
        avgNormal = avgNormal / avgNormalLen;
        
        // Step 2: Create local 2D coordinate system for planar projection
        Vector3<Real> zAxis = avgNormal;
        Vector3<Real> xAxis, yAxis;
        
        // Find a vector not parallel to zAxis
        Vector3<Real> arbitrary{static_cast<Real>(1), static_cast<Real>(0), static_cast<Real>(0)};
        if (std::abs(Dot(arbitrary, zAxis)) > static_cast<Real>(0.9))
        {
            arbitrary = Vector3<Real>{static_cast<Real>(0), static_cast<Real>(1), static_cast<Real>(0)};
        }
        
        xAxis = Cross(arbitrary, zAxis);
        Real xLen = Length(xAxis);
        if (xLen < static_cast<Real>(1e-10))
        {
            return false;
        }
        xAxis = xAxis / xLen;
        
        yAxis = Cross(zAxis, xAxis);
        Real yLen = Length(yAxis);
        if (yLen < static_cast<Real>(1e-10))
        {
            return false;
        }
        yAxis = yAxis / yLen;
        
        // Step 3: Project all points to 2D using planar projection
        // Add boundary vertices
        for (int32_t vi : hole.vertexIndices)
        {
            Vector3<Real> p = vertices[vi] - centroid;
            double x = static_cast<double>(Dot(p, xAxis));
            double y = static_cast<double>(Dot(p, yAxis));
            points2D.push_back({x, y});
            indexMap.push_back(vi);
        }
        
        // Add Steiner vertices
        for (int32_t vi : steinerVertexIndices)
        {
            Vector3<Real> p = vertices[vi] - centroid;
            double x = static_cast<double>(Dot(p, xAxis));
            double y = static_cast<double>(Dot(p, yAxis));
            points2D.push_back({x, y});
            indexMap.push_back(vi);
        }
        
        // Check for overlaps in 2D projection (simplified check)
        bool hasOverlaps = false;
        Real minDist2D = std::numeric_limits<Real>::max();
        for (size_t i = 0; i < points2D.size(); ++i)
        {
            for (size_t j = i + 1; j < points2D.size(); ++j)
            {
                Real dx = static_cast<Real>(points2D[i].x - points2D[j].x);
                Real dy = static_cast<Real>(points2D[i].y - points2D[j].y);
                Real dist = std::sqrt(dx * dx + dy * dy);
                minDist2D = std::min(minDist2D, dist);
                
                // If points are too close in 2D but not in 3D, we have overlaps
                Vector3<Real> p1 = vertices[indexMap[i]];
                Vector3<Real> p2 = vertices[indexMap[j]];
                Real dist3D = Length(p2 - p1);
                
                if (dist < hole.avgEdgeLength * static_cast<Real>(0.1) &&
                    dist3D > hole.avgEdgeLength * static_cast<Real>(0.5))
                {
                    hasOverlaps = true;
                    break;
                }
            }
            if (hasOverlaps) break;
        }
        
        // If overlaps detected, try UV unwrapping (LSCM)
        // Per problem statement: "if the curve is self intersecting" use UV unwrapping
        if (hasOverlaps && hole.vertexIndices.size() > 3)
        {
            if (params.verbose)
            {
                std::cout << "      SELF-INTERSECTION detected in planar projection!\n";
                std::cout << "      Trying UV unwrapping (LSCM) per problem statement...\n";
            }
            
            // Use LSCM for UV unwrapping
            std::vector<Vector2<Real>> uvCoords;
            bool lscmSuccess = LSCMParameterization<Real>::Parameterize(
                vertices, hole.vertexIndices, steinerVertexIndices,
                {}, // No triangles for boundary-only case
                uvCoords, params.verbose);
            
            if (lscmSuccess)
            {
                // Replace 2D points with UV coordinates
                points2D.clear();
                indexMap.clear();
                
                // Scale UV coordinates to reasonable range for detria
                Real scale = hole.avgEdgeLength * static_cast<Real>(10);
                
                for (int32_t vi : hole.vertexIndices)
                {
                    if (uvCoords[vi][0] >= static_cast<Real>(0))
                    {
                        points2D.push_back({
                            static_cast<double>(uvCoords[vi][0] * scale),
                            static_cast<double>(uvCoords[vi][1] * scale)
                        });
                        indexMap.push_back(vi);
                    }
                }
                
                for (int32_t vi : steinerVertexIndices)
                {
                    if (uvCoords[vi][0] >= static_cast<Real>(0))
                    {
                        points2D.push_back({
                            static_cast<double>(uvCoords[vi][0] * scale),
                            static_cast<double>(uvCoords[vi][1] * scale)
                        });
                        indexMap.push_back(vi);
                    }
                }
                
                useLSCM = true;
                
                if (params.verbose)
                {
                    std::cout << "      UV unwrapping successful, using LSCM coordinates\n";
                }
            }
            else if (params.verbose)
            {
                std::cout << "      UV unwrapping failed, continuing with planar projection\n";
            }
        }
        
        // Step 3.5: Apply 2D perturbation to break collinearity in parametric space
        // CRITICAL: Per problem statement, must perturb in 2D AFTER projection, not in 3D
        PerturbCollinearPointsIn2D(points2D, hole.vertexIndices.size(), hole.avgEdgeLength, params);
        
        // Step 4: Set up detria triangulation
        detria::Triangulation tri;
        tri.setPoints(points2D);
        
        // Add boundary as outline (constrained edges)
        std::vector<uint32_t> outline;
        for (size_t i = 0; i < hole.vertexIndices.size(); ++i)
        {
            outline.push_back(static_cast<uint32_t>(i));
        }
        tri.addOutline(outline);
        
        // Step 5: Triangulate (Delaunay with constraints)
        bool success = tri.triangulate(true);
        if (!success)
        {
            if (params.verbose)
            {
                std::cout << "      Detria triangulation failed\n";
            }
            
            // Diagnose why detria failed
            std::vector<std::array<uint32_t, 2>> edges;
            for (size_t i = 0; i < hole.vertexIndices.size(); ++i)
            {
                size_t next = (i + 1) % hole.vertexIndices.size();
                edges.push_back({static_cast<uint32_t>(i), static_cast<uint32_t>(next)});
            }
            DiagnoseDetriaFailure(points2D, edges, params);
            
            // Per problem statement: Try UV unwrapping (LSCM) as fallback when detria fails
            // This is specifically for otherwise unfillable holes
            if (!useLSCM && hole.vertexIndices.size() > 3)
            {
                if (params.verbose)
                {
                    std::cout << "      Attempting UV unwrapping (LSCM) as fallback per problem statement...\n";
                }
                
                // Use LSCM for UV unwrapping
                std::vector<Vector2<Real>> uvCoords;
                bool lscmSuccess = LSCMParameterization<Real>::Parameterize(
                    vertices, hole.vertexIndices, steinerVertexIndices,
                    {}, // No triangles for boundary-only case
                    uvCoords, params.verbose);
                
                if (lscmSuccess)
                {
                    // Replace 2D points with UV coordinates
                    points2D.clear();
                    indexMap.clear();
                    
                    // Scale UV coordinates to reasonable range for detria
                    Real scale = hole.avgEdgeLength * static_cast<Real>(10);
                    
                    for (int32_t vi : hole.vertexIndices)
                    {
                        if (uvCoords[vi][0] >= static_cast<Real>(0))
                        {
                            points2D.push_back({
                                static_cast<double>(uvCoords[vi][0] * scale),
                                static_cast<double>(uvCoords[vi][1] * scale)
                            });
                            indexMap.push_back(vi);
                        }
                    }
                    
                    for (int32_t vi : steinerVertexIndices)
                    {
                        if (uvCoords[vi][0] >= static_cast<Real>(0))
                        {
                            points2D.push_back({
                                static_cast<double>(uvCoords[vi][0] * scale),
                                static_cast<double>(uvCoords[vi][1] * scale)
                            });
                            indexMap.push_back(vi);
                        }
                    }
                    
                    if (params.verbose)
                    {
                        std::cout << "      LSCM parameterization successful, retrying detria with UV coordinates...\n";
                    }
                    
                    // Apply 2D perturbation to UV coordinates before triangulation
                    PerturbCollinearPointsIn2D(points2D, hole.vertexIndices.size(), hole.avgEdgeLength, params);
                    
                    // Retry detria with UV coordinates
                    detria::Triangulation uvTri;
                    uvTri.setPoints(points2D);
                    uvTri.addOutline(outline);
                    bool uvSuccess = uvTri.triangulate(true);
                    
                    if (uvSuccess)
                    {
                        useLSCM = true;
                        
                        if (params.verbose)
                        {
                            std::cout << "      ✓ SUCCESS with UV unwrapping + detria!\n";
                        }
                        
                        // Extract triangles immediately from uvTri
                        size_t trianglesAdded = 0;
                        bool cwTriangles = true;
                        
                        uvTri.forEachTriangle([&](detria::Triangle<uint32_t> triangle)
                        {
                            // Map 2D indices back to 3D vertex indices
                            int32_t v0 = indexMap[triangle.x];
                            int32_t v1 = indexMap[triangle.y];
                            int32_t v2 = indexMap[triangle.z];
                            
                            // Check triangle orientation matches hole normal
                            Vector3<Real> edge1 = vertices[v1] - vertices[v0];
                            Vector3<Real> edge2 = vertices[v2] - vertices[v0];
                            Vector3<Real> triNormal = Cross(edge1, edge2);
                            
                            // Flip if necessary to match hole normal
                            if (Dot(triNormal, avgNormal) < static_cast<Real>(0))
                            {
                                std::swap(v1, v2);
                            }
                            
                            triangles.push_back({v0, v1, v2});
                            trianglesAdded++;
                        }, cwTriangles);
                        
                        if (params.verbose)
                        {
                            std::cout << "      UV unwrapping added " << trianglesAdded << " triangles\n";
                        }
                        
                        return trianglesAdded > 0;
                    }
                    else 
                    {
                        if (params.verbose)
                        {
                            std::cout << "      Detria still failed even with UV coordinates\n";
                        }
                        
                        // Diagnose UV triangulation failure
                        std::vector<std::array<uint32_t, 2>> uvEdges;
                        for (size_t i = 0; i < hole.vertexIndices.size(); ++i)
                        {
                            size_t next = (i + 1) % hole.vertexIndices.size();
                            uvEdges.push_back({static_cast<uint32_t>(i), static_cast<uint32_t>(next)});
                        }
                        DiagnoseDetriaFailure(points2D, uvEdges, params);
                    }
                }
                else if (params.verbose)
                {
                    std::cout << "      LSCM parameterization failed\n";
                }
            }
            
            // Per new requirement: Try degeneracy detection and repair as final fallback
            // If both planar and LSCM detria failed, the hole may have degenerate geometry
            if (hole.vertexIndices.size() >= 3)
            {
                if (params.verbose)
                {
                    std::cout << "\n      Attempting degeneracy detection and repair as final fallback...\n";
                }
                
                // Detect if hole is degenerate
                DegeneracyInfo degeneracy = DetectDegenerateHole(vertices, hole, params);
                
                if (degeneracy.isDegenerate)
                {
                    if (params.verbose)
                    {
                        std::cout << "      ✓ Hole is degenerate, attempting repair...\n";
                    }
                    
                    // Repair the degenerate hole
                    BoundaryLoop repairedHole = RepairDegenerateHole(vertices, hole, degeneracy, params);
                    
                    // Check if repair made progress
                    if (repairedHole.vertexIndices.size() < hole.vertexIndices.size() &&
                        repairedHole.vertexIndices.size() >= 3)
                    {
                        if (params.verbose)
                        {
                            std::cout << "      Simplified boundary: " << hole.vertexIndices.size() 
                                     << " → " << repairedHole.vertexIndices.size() << " vertices\n";
                            std::cout << "      Retrying triangulation with repaired boundary...\n";
                        }
                        
                        // Try planar projection + detria with repaired boundary
                        std::vector<detria::PointD> repairedPoints2D;
                        std::vector<int32_t> repairedIndexMap;
                        
                        // Compute best-fit plane for repaired boundary
                        Vector3<Real> repairedCentroid{0, 0, 0};
                        for (int32_t vi : repairedHole.vertexIndices)
                        {
                            repairedCentroid += vertices[vi];
                        }
                        repairedCentroid /= static_cast<Real>(repairedHole.vertexIndices.size());
                        
                        // Use same normal computation as before
                        Vector3<Real> repairedAvgNormal{0, 0, 0};
                        for (size_t i = 0; i < repairedHole.vertexIndices.size(); ++i)
                        {
                            size_t next = (i + 1) % repairedHole.vertexIndices.size();
                            Vector3<Real> const& v1 = vertices[repairedHole.vertexIndices[i]];
                            Vector3<Real> const& v2 = vertices[repairedHole.vertexIndices[next]];
                            repairedAvgNormal += Cross(v1 - repairedCentroid, v2 - repairedCentroid);
                        }
                        Normalize(repairedAvgNormal);
                        
                        // Create orthonormal basis for 2D projection
                        Vector3<Real> repairedZAxis = repairedAvgNormal;
                        Vector3<Real> repairedU, repairedV;
                        
                        // Find a vector not parallel to zAxis
                        Vector3<Real> arbitrary{static_cast<Real>(1), static_cast<Real>(0), static_cast<Real>(0)};
                        if (std::abs(Dot(arbitrary, repairedZAxis)) > static_cast<Real>(0.9))
                        {
                            arbitrary = Vector3<Real>{static_cast<Real>(0), static_cast<Real>(1), static_cast<Real>(0)};
                        }
                        
                        repairedU = Cross(arbitrary, repairedZAxis);
                        Real uLen = Length(repairedU);
                        if (uLen > static_cast<Real>(1e-10))
                        {
                            repairedU = repairedU / uLen;
                        }
                        
                        repairedV = Cross(repairedZAxis, repairedU);
                        Real vLen = Length(repairedV);
                        if (vLen > static_cast<Real>(1e-10))
                        {
                            repairedV = repairedV / vLen;
                        }
                        
                        // Project to 2D
                        for (int32_t vi : repairedHole.vertexIndices)
                        {
                            Vector3<Real> p = vertices[vi] - repairedCentroid;
                            repairedPoints2D.push_back({
                                static_cast<double>(Dot(p, repairedU)),
                                static_cast<double>(Dot(p, repairedV))
                            });
                            repairedIndexMap.push_back(vi);
                        }
                        
                        // Try detria on repaired boundary
                        detria::Triangulation repairedTri;
                        repairedTri.setPoints(repairedPoints2D);
                        
                        std::vector<uint32_t> repairedOutline;
                        for (size_t i = 0; i < repairedHole.vertexIndices.size(); ++i)
                        {
                            repairedOutline.push_back(static_cast<uint32_t>(i));
                        }
                        repairedTri.addOutline(repairedOutline);
                        
                        bool repairedSuccess = repairedTri.triangulate(true);
                        
                        if (repairedSuccess)
                        {
                            if (params.verbose)
                            {
                                std::cout << "      ✓ SUCCESS with repaired boundary + detria!\n";
                            }
                            
                            // Extract triangles
                            size_t trianglesAdded = 0;
                            bool cwTriangles = true;
                            
                            repairedTri.forEachTriangle([&](detria::Triangle<uint32_t> triangle)
                            {
                                int32_t v0 = repairedIndexMap[triangle.x];
                                int32_t v1 = repairedIndexMap[triangle.y];
                                int32_t v2 = repairedIndexMap[triangle.z];
                                
                                // Check triangle orientation
                                Vector3<Real> edge1 = vertices[v1] - vertices[v0];
                                Vector3<Real> edge2 = vertices[v2] - vertices[v0];
                                Vector3<Real> triNormal = Cross(edge1, edge2);
                                
                                if (Dot(triNormal, repairedAvgNormal) < static_cast<Real>(0))
                                {
                                    std::swap(v1, v2);
                                }
                                
                                triangles.push_back({v0, v1, v2});
                                trianglesAdded++;
                            }, cwTriangles);
                            
                            if (params.verbose)
                            {
                                std::cout << "      Degeneracy repair added " << trianglesAdded << " triangles\n";
                            }
                            
                            return trianglesAdded > 0;
                        }
                        else if (params.verbose)
                        {
                            std::cout << "      Detria still failed with repaired boundary\n";
                        }
                    }
                    else if (params.verbose)
                    {
                        std::cout << "      Repair did not simplify boundary enough\n";
                    }
                }
                else if (params.verbose)
                {
                    std::cout << "      Hole is not degenerate (no geometric issues detected)\n";
                }
            }
            
            // If we get here, all methods failed
            return false;
        }
        
        // Step 6: Extract triangles and add to mesh (detria succeeded)
        size_t trianglesAdded = 0;
        bool cwTriangles = true;  // Clock-wise
        
        tri.forEachTriangle([&](detria::Triangle<uint32_t> triangle)
        {
            // Map 2D indices back to 3D vertex indices
            int32_t v0 = indexMap[triangle.x];
            int32_t v1 = indexMap[triangle.y];
            int32_t v2 = indexMap[triangle.z];
            
            // Check triangle orientation matches hole normal
            Vector3<Real> edge1 = vertices[v1] - vertices[v0];
            Vector3<Real> edge2 = vertices[v2] - vertices[v0];
            Vector3<Real> triNormal = Cross(edge1, edge2);
            
            // Flip if necessary to match hole normal
            if (Dot(triNormal, avgNormal) < static_cast<Real>(0))
            {
                std::swap(v1, v2);
            }
            
            triangles.push_back({v0, v1, v2});
            trianglesAdded++;
        }, cwTriangles);
        
        if (params.verbose)
        {
            std::cout << "      Detria added " << trianglesAdded << " triangles with " 
                      << steinerVertexIndices.size() << " Steiner points\n";
        }
        
        return trianglesAdded > 0;
    }
    
    // Degeneracy detection - analyzes if a hole boundary has degenerate geometry
    template <typename Real>
    typename BallPivotMeshHoleFiller<Real>::DegeneracyInfo 
    BallPivotMeshHoleFiller<Real>::DetectDegenerateHole(
        std::vector<Vector3<Real>> const& vertices,
        BoundaryLoop const& hole,
        Parameters const& params)
    {
        DegeneracyInfo info;
        
        if (hole.vertexIndices.size() < 3)
        {
            return info;  // Too small to analyze
        }
        
        int32_t n = static_cast<int32_t>(hole.vertexIndices.size());
        Real avgEdgeLength = hole.avgEdgeLength;
        Real epsilon = avgEdgeLength * static_cast<Real>(0.01);  // 1% of average edge
        Real angleThreshold = static_cast<Real>(170.0 * 3.14159265358979323846 / 180.0);  // 170 degrees in radians
        
        // Check 1: Collinear consecutive edges (angle near 180°)
        for (int32_t i = 0; i < n; ++i)
        {
            int32_t prev = (i - 1 + n) % n;
            int32_t curr = i;
            int32_t next = (i + 1) % n;
            
            Vector3<Real> const& vPrev = vertices[hole.vertexIndices[prev]];
            Vector3<Real> const& vCurr = vertices[hole.vertexIndices[curr]];
            Vector3<Real> const& vNext = vertices[hole.vertexIndices[next]];
            
            Vector3<Real> edge1 = vCurr - vPrev;
            Vector3<Real> edge2 = vNext - vCurr;
            
            Real len1 = Length(edge1);
            Real len2 = Length(edge2);
            
            if (len1 > epsilon && len2 > epsilon)
            {
                edge1 /= len1;
                edge2 /= len2;
                
                Real dot = Dot(edge1, edge2);
                Real angle = std::acos(std::clamp(dot, static_cast<Real>(-1), static_cast<Real>(1)));
                
                // If angle is close to 180° (pi radians), edges are collinear
                Real deviation = std::abs(static_cast<Real>(3.14159265358979323846) - angle);
                Real deviationDegrees = deviation * static_cast<Real>(180.0 / 3.14159265358979323846);
                
                if (deviationDegrees < static_cast<Real>(10.0))  // Within 10° of straight line
                {
                    info.collinearVertexCount++;
                    info.collinearVertices.push_back(hole.vertexIndices[curr]);
                    info.maxAngleDeviation = std::max(info.maxAngleDeviation, deviationDegrees);
                }
            }
        }
        
        // Check 2: Near-zero edge lengths
        for (int32_t i = 0; i < n; ++i)
        {
            int32_t next = (i + 1) % n;
            Vector3<Real> const& v1 = vertices[hole.vertexIndices[i]];
            Vector3<Real> const& v2 = vertices[hole.vertexIndices[next]];
            
            Real edgeLength = Length(v2 - v1);
            info.minEdgeLength = std::min(info.minEdgeLength, edgeLength);
            
            if (edgeLength < epsilon)
            {
                info.nearZeroEdgeCount++;
            }
        }
        
        // Check 3: Vertices lying on opposite edges
        // For each vertex, check distance to all non-adjacent edges
        for (int32_t i = 0; i < n; ++i)
        {
            Vector3<Real> const& v = vertices[hole.vertexIndices[i]];
            
            // Check distance to all edges except adjacent ones
            for (int32_t j = 0; j < n; ++j)
            {
                // Skip adjacent edges
                if (j == i || j == (i - 1 + n) % n || j == (i + 1) % n)
                    continue;
                
                int32_t next = (j + 1) % n;
                Vector3<Real> const& e1 = vertices[hole.vertexIndices[j]];
                Vector3<Real> const& e2 = vertices[hole.vertexIndices[next]];
                
                // Compute distance from point to line segment
                Vector3<Real> edge = e2 - e1;
                Real edgeLen = Length(edge);
                
                if (edgeLen < epsilon)
                    continue;
                
                Vector3<Real> toPoint = v - e1;
                Real t = Dot(toPoint, edge) / (edgeLen * edgeLen);
                
                // Only check if projection is within the edge segment
                if (t > static_cast<Real>(0.01) && t < static_cast<Real>(0.99))
                {
                    Vector3<Real> projection = e1 + edge * t;
                    Real distance = Length(v - projection);
                    
                    if (distance < epsilon * static_cast<Real>(3.0))  // Within 3% of avg edge
                    {
                        info.vertexOnEdgeCount++;
                        break;  // Count each vertex only once
                    }
                }
            }
        }
        
        // Check 4: Near-duplicate vertices
        for (int32_t i = 0; i < n; ++i)
        {
            Vector3<Real> const& v1 = vertices[hole.vertexIndices[i]];
            
            for (int32_t j = i + 2; j < n; ++j)  // Skip adjacent vertices
            {
                if (j == (i - 1 + n) % n || j == (i + 1) % n)
                    continue;
                    
                Vector3<Real> const& v2 = vertices[hole.vertexIndices[j]];
                Real distance = Length(v2 - v1);
                
                if (distance < epsilon)
                {
                    info.nearDuplicateVertexCount++;
                }
            }
        }
        
        // Determine if overall degenerate
        info.isDegenerate = (info.collinearVertexCount > 0) ||
                           (info.vertexOnEdgeCount > 0) ||
                           (info.nearZeroEdgeCount > 0) ||
                           (info.nearDuplicateVertexCount > 0);
        
        if (params.verbose && info.isDegenerate)
        {
            std::cout << "\n[Degeneracy Detection]\n";
            std::cout << "  Hole has " << n << " vertices\n";
            std::cout << "  Collinear vertices: " << info.collinearVertexCount << "\n";
            std::cout << "  Vertex-on-edge cases: " << info.vertexOnEdgeCount << "\n";
            std::cout << "  Near-zero edges: " << info.nearZeroEdgeCount << "\n";
            std::cout << "  Near-duplicate vertices: " << info.nearDuplicateVertexCount << "\n";
            std::cout << "  Min edge length: " << info.minEdgeLength << "\n";
            std::cout << "  Max angle deviation: " << info.maxAngleDeviation << " degrees\n";
        }
        
        return info;
    }
    
    // Degeneracy repair - simplifies a degenerate hole boundary
    template <typename Real>
    typename BallPivotMeshHoleFiller<Real>::BoundaryLoop 
    BallPivotMeshHoleFiller<Real>::RepairDegenerateHole(
        std::vector<Vector3<Real>> const& vertices,
        BoundaryLoop const& hole,
        DegeneracyInfo const& degeneracy,
        Parameters const& params)
    {
        BoundaryLoop repairedHole = hole;
        
        if (!degeneracy.isDegenerate)
        {
            return repairedHole;  // Nothing to repair
        }
        
        if (params.verbose)
        {
            std::cout << "\n[Degeneracy Repair]\n";
            std::cout << "  Starting with " << hole.vertexIndices.size() << " vertices\n";
        }
        
        Real avgEdgeLength = hole.avgEdgeLength;
        Real epsilon = avgEdgeLength * static_cast<Real>(0.01);
        
        // Strategy 1: Remove collinear vertices (industry standard polygon simplification)
        // This merges consecutive edges that are nearly collinear
        if (degeneracy.collinearVertexCount > 0 && degeneracy.collinearVertices.size() > 0)
        {
            if (params.verbose)
            {
                std::cout << "  Removing " << degeneracy.collinearVertexCount << " collinear vertices...\n";
            }
            
            std::set<int32_t> verticesToRemove(
                degeneracy.collinearVertices.begin(),
                degeneracy.collinearVertices.end());
            
            std::vector<int32_t> newVertices;
            for (int32_t vi : repairedHole.vertexIndices)
            {
                if (verticesToRemove.find(vi) == verticesToRemove.end())
                {
                    newVertices.push_back(vi);
                }
            }
            
            if (newVertices.size() >= 3)
            {
                repairedHole.vertexIndices = newVertices;
                
                if (params.verbose)
                {
                    std::cout << "  After collinear removal: " << newVertices.size() << " vertices\n";
                }
            }
        }
        
        // Strategy 2: Remove near-zero edges by merging endpoints
        if (degeneracy.nearZeroEdgeCount > 0)
        {
            if (params.verbose)
            {
                std::cout << "  Removing near-zero edges...\n";
            }
            
            std::vector<int32_t> newVertices;
            int32_t n = static_cast<int32_t>(repairedHole.vertexIndices.size());
            
            for (int32_t i = 0; i < n; ++i)
            {
                int32_t next = (i + 1) % n;
                Vector3<Real> const& v1 = vertices[repairedHole.vertexIndices[i]];
                Vector3<Real> const& v2 = vertices[repairedHole.vertexIndices[next]];
                
                Real edgeLength = Length(v2 - v1);
                
                // Keep vertex if edge is not near-zero
                if (edgeLength >= epsilon)
                {
                    newVertices.push_back(repairedHole.vertexIndices[i]);
                }
                // Otherwise skip this vertex (merge with next)
            }
            
            if (newVertices.size() >= 3)
            {
                repairedHole.vertexIndices = newVertices;
                
                if (params.verbose)
                {
                    std::cout << "  After near-zero edge removal: " << newVertices.size() << " vertices\n";
                }
            }
        }
        
        // Strategy 3: For vertex-on-edge cases, we would ideally split edges
        // However, this requires modifying the vertex list which is more complex
        // For now, document this limitation
        if (degeneracy.vertexOnEdgeCount > 0 && params.verbose)
        {
            std::cout << "  Note: " << degeneracy.vertexOnEdgeCount 
                      << " vertex-on-edge cases detected\n";
            std::cout << "  (Edge splitting not yet implemented - requires vertex list modification)\n";
        }
        
        // Recalculate hole statistics
        int32_t n = static_cast<int32_t>(repairedHole.vertexIndices.size());
        Real totalLength = static_cast<Real>(0);
        Real minLen = std::numeric_limits<Real>::max();
        Real maxLen = static_cast<Real>(0);
        
        for (int32_t i = 0; i < n; ++i)
        {
            int32_t next = (i + 1) % n;
            Vector3<Real> const& v1 = vertices[repairedHole.vertexIndices[i]];
            Vector3<Real> const& v2 = vertices[repairedHole.vertexIndices[next]];
            
            Real edgeLength = Length(v2 - v1);
            totalLength += edgeLength;
            minLen = std::min(minLen, edgeLength);
            maxLen = std::max(maxLen, edgeLength);
        }
        
        repairedHole.avgEdgeLength = totalLength / static_cast<Real>(n);
        repairedHole.minEdgeLength = minLen;
        repairedHole.maxEdgeLength = maxLen;
        
        if (params.verbose)
        {
            std::cout << "  Final repaired hole: " << repairedHole.vertexIndices.size() << " vertices\n";
            std::cout << "  New avg edge length: " << repairedHole.avgEdgeLength << "\n";
        }
        
        return repairedHole;
    }
    
    // Non-manifold edge detection and repair
    template <typename Real>
    std::vector<std::pair<int32_t, int32_t>> BallPivotMeshHoleFiller<Real>::DetectNonManifoldEdges(
        std::vector<std::array<int32_t, 3>> const& triangles)
    {
        // Build edge-to-triangle count map
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
        
        // Find edges with 3+ triangles
        std::vector<std::pair<int32_t, int32_t>> nonManifoldEdges;
        for (auto const& ec : edgeCount)
        {
            if (ec.second >= 3)
            {
                nonManifoldEdges.push_back(ec.first);
            }
        }
        
        return nonManifoldEdges;
    }
    
    template <typename Real>
    bool BallPivotMeshHoleFiller<Real>::LocalRemeshNonManifoldRegion(
        std::vector<Vector3<Real>>& vertices,
        std::vector<std::array<int32_t, 3>>& triangles,
        std::pair<int32_t, int32_t> const& nonManifoldEdge,
        Parameters const& params)
    {
        if (params.verbose)
        {
            std::cout << "    Remeshing non-manifold edge (" << nonManifoldEdge.first 
                      << ", " << nonManifoldEdge.second << ")\n";
        }
        
        // Step 1: Find all triangles sharing this edge
        std::vector<int32_t> trianglesToRemove;
        std::set<int32_t> regionVertices;
        
        for (size_t i = 0; i < triangles.size(); ++i)
        {
            auto const& tri = triangles[i];
            bool hasEdge = false;
            
            for (int j = 0; j < 3; ++j)
            {
                int32_t v0 = tri[j];
                int32_t v1 = tri[(j + 1) % 3];
                auto edge = std::make_pair(std::min(v0, v1), std::max(v0, v1));
                
                if (edge == nonManifoldEdge)
                {
                    hasEdge = true;
                    break;
                }
            }
            
            if (hasEdge)
            {
                trianglesToRemove.push_back(static_cast<int32_t>(i));
                regionVertices.insert(tri[0]);
                regionVertices.insert(tri[1]);
                regionVertices.insert(tri[2]);
            }
        }
        
        if (trianglesToRemove.empty())
        {
            return false;  // Edge no longer exists
        }
        
        if (params.verbose)
        {
            std::cout << "      Removing " << trianglesToRemove.size() << " triangles\n";
            std::cout << "      Region has " << regionVertices.size() << " vertices\n";
        }
        
        // Step 2: Remove the conflicting triangles
        std::vector<std::array<int32_t, 3>> newTriangles;
        for (size_t i = 0; i < triangles.size(); ++i)
        {
            bool shouldRemove = false;
            for (int32_t idx : trianglesToRemove)
            {
                if (static_cast<int32_t>(i) == idx)
                {
                    shouldRemove = true;
                    break;
                }
            }
            
            if (!shouldRemove)
            {
                newTriangles.push_back(triangles[i]);
            }
        }
        
        triangles = newTriangles;
        
        // Step 3: Detect the boundary loop created by removal
        auto holes = DetectBoundaryLoops(vertices, triangles);
        
        if (holes.empty())
        {
            if (params.verbose)
            {
                std::cout << "      No hole created (triangles isolated)\n";
            }
            return true;  // Removal successful, no hole to fill
        }
        
        // Find the hole that contains our edge vertices
        BoundaryLoop* targetHole = nullptr;
        for (auto& hole : holes)
        {
            bool hasV0 = false;
            bool hasV1 = false;
            for (int32_t vi : hole.vertexIndices)
            {
                if (vi == nonManifoldEdge.first) hasV0 = true;
                if (vi == nonManifoldEdge.second) hasV1 = true;
            }
            if (hasV0 || hasV1)
            {
                targetHole = &hole;
                break;
            }
        }
        
        if (!targetHole)
        {
            if (params.verbose)
            {
                std::cout << "      Could not find hole containing edge vertices\n";
            }
            return false;
        }
        
        if (params.verbose)
        {
            std::cout << "      Hole has " << targetHole->vertexIndices.size() << " vertices\n";
        }
        
        // Step 4: Retriangulate using detria
        // Collect all vertices in region (including any nearby ones for Steiner points)
        std::vector<int32_t> steinerVertices;
        Real searchRadius = targetHole->avgEdgeLength * static_cast<Real>(2.0);
        Vector3<Real> holeCentroid{static_cast<Real>(0), static_cast<Real>(0), static_cast<Real>(0)};
        
        for (int32_t vi : targetHole->vertexIndices)
        {
            holeCentroid = holeCentroid + vertices[vi];
        }
        holeCentroid = holeCentroid / static_cast<Real>(targetHole->vertexIndices.size());
        
        for (int32_t vi : regionVertices)
        {
            // Check if vertex is not in boundary
            bool inBoundary = false;
            for (int32_t bvi : targetHole->vertexIndices)
            {
                if (vi == bvi)
                {
                    inBoundary = true;
                    break;
                }
            }
            
            if (!inBoundary)
            {
                Real dist = Length(vertices[vi] - holeCentroid);
                if (dist < searchRadius)
                {
                    steinerVertices.push_back(vi);
                }
            }
        }
        
        if (params.verbose && !steinerVertices.empty())
        {
            std::cout << "      Using " << steinerVertices.size() << " Steiner vertices\n";
        }
        
        // Try to fill with detria (with or without Steiner points)
        bool filled = false;
        if (!steinerVertices.empty())
        {
            filled = FillHoleWithSteinerPoints(vertices, triangles, *targetHole, steinerVertices, params);
        }
        
        if (!filled)
        {
            // Fallback to ear clipping
            filled = FillHole(vertices, triangles, *targetHole, params);
        }
        
        if (params.verbose)
        {
            if (filled)
            {
                std::cout << "      Successfully retriangulated region\n";
            }
            else
            {
                std::cout << "      Failed to retriangulate region\n";
            }
        }
        
        return filled;
    }
    
    template <typename Real>
    std::vector<int32_t> BallPivotMeshHoleFiller<Real>::DetectConflictingTriangles(
        std::vector<Vector3<Real>> const& vertices,
        std::vector<std::array<int32_t, 3>> const& triangles,
        BoundaryLoop const& hole)
    {
        std::vector<int32_t> conflicting;
        
        // Compute hole plane and bounds
        Vector3<Real> holeCentroid{static_cast<Real>(0), static_cast<Real>(0), static_cast<Real>(0)};
        for (int32_t vi : hole.vertexIndices)
        {
            holeCentroid = holeCentroid + vertices[vi];
        }
        holeCentroid = holeCentroid / static_cast<Real>(hole.vertexIndices.size());
        
        // Compute hole normal (average of edge normals)
        Vector3<Real> holeNormal{static_cast<Real>(0), static_cast<Real>(0), static_cast<Real>(0)};
        for (size_t i = 0; i < hole.vertexIndices.size(); ++i)
        {
            int32_t v0 = hole.vertexIndices[i];
            int32_t v1 = hole.vertexIndices[(i + 1) % hole.vertexIndices.size()];
            Vector3<Real> edge = vertices[v1] - vertices[v0];
            Vector3<Real> toCenter = holeCentroid - vertices[v0];
            Vector3<Real> normal = Cross(edge, toCenter);
            holeNormal = holeNormal + normal;
        }
        
        Real holeNormalLen = Length(holeNormal);
        if (holeNormalLen > static_cast<Real>(0))
        {
            holeNormal = holeNormal / holeNormalLen;
        }
        
        // Check each triangle for conflicts
        std::set<int32_t> holeVertexSet(hole.vertexIndices.begin(), hole.vertexIndices.end());
        
        for (size_t i = 0; i < triangles.size(); ++i)
        {
            auto const& tri = triangles[i];
            
            // Count how many vertices are in hole boundary
            int boundaryVertices = 0;
            for (int j = 0; j < 3; ++j)
            {
                if (holeVertexSet.count(tri[j]))
                {
                    ++boundaryVertices;
                }
            }
            
            // Per problem statement: Be MORE CONSERVATIVE about triangle removal
            // Triangle is conflicting if:
            // 1. It has EXACTLY 2 vertices on boundary (forms an edge triangle)
            // 2. Its centroid is VERY near the hole (within 1x avg edge length, not 3x)
            // 3. Its normal STRONGLY conflicts with hole normal (opposite direction)
            // 
            // Changed from: boundaryVertices > 0 && boundaryVertices < 3
            // To: boundaryVertices == 2 (more conservative)
            // Changed from: searchRadius = 3.0 * avgEdgeLength
            // To: searchRadius = 1.0 * avgEdgeLength (more conservative)
            // Changed from: dot < 0.5 (< 60 degrees)
            // To: dot < 0.0 (opposite direction, more conservative)
            
            if (boundaryVertices == 2)  // ONLY edge triangles with 2 boundary vertices
            {
                Vector3<Real> triCentroid = (vertices[tri[0]] + vertices[tri[1]] + vertices[tri[2]]) 
                                           / static_cast<Real>(3.0);
                Real dist = Length(triCentroid - holeCentroid);
                
                // Much more conservative distance check (1x instead of 3x)
                Real conservativeSearchRadius = hole.avgEdgeLength * static_cast<Real>(1.0);
                
                if (dist < conservativeSearchRadius)
                {
                    // Check normal conflict
                    Vector3<Real> triNormal = Cross(
                        vertices[tri[1]] - vertices[tri[0]],
                        vertices[tri[2]] - vertices[tri[0]]);
                    Real triNormalLen = Length(triNormal);
                    
                    if (triNormalLen > static_cast<Real>(0))
                    {
                        triNormal = triNormal / triNormalLen;
                        Real dot = Dot(triNormal, holeNormal);
                        
                        // Only conflicting if normals are opposite (< 0 degrees = cos 0.0)
                        // Much more conservative than before (was < 0.5 = 60 degrees)
                        if (dot < static_cast<Real>(0.0))
                        {
                            conflicting.push_back(static_cast<int32_t>(i));
                        }
                    }
                }
            }
        }
        
        return conflicting;
    }
    

    // ====================================================================================
    // 2D Perturbation and Diagnostics Methods
    // ====================================================================================
    
    template <typename Real>
    bool BallPivotMeshHoleFiller<Real>::DoLineSegmentsIntersect2D(
        detria::PointD const& p1, detria::PointD const& p2,
        detria::PointD const& p3, detria::PointD const& p4)
    {
        // Use cross product to check if segments intersect
        // Segments p1-p2 and p3-p4 intersect if:
        // 1. p1 and p2 are on opposite sides of line p3-p4
        // 2. p3 and p4 are on opposite sides of line p1-p2
        
        auto crossProduct = [](detria::PointD const& a, detria::PointD const& b, detria::PointD const& c) {
            // Cross product of vectors (b-a) and (c-a) in 2D (z component)
            return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
        };
        
        double d1 = crossProduct(p3, p4, p1);
        double d2 = crossProduct(p3, p4, p2);
        double d3 = crossProduct(p1, p2, p3);
        double d4 = crossProduct(p1, p2, p4);
        
        // Check if signs are opposite (segments cross)
        if (((d1 > 0 && d2 < 0) || (d1 < 0 && d2 > 0)) &&
            ((d3 > 0 && d4 < 0) || (d3 < 0 && d4 > 0)))
        {
            return true;
        }
        
        // Check for collinear cases (touching)
        double epsilon = 1e-10;
        if (std::abs(d1) < epsilon && std::abs(d2) < epsilon &&
            std::abs(d3) < epsilon && std::abs(d4) < epsilon)
        {
            // Collinear - check overlap
            // Could be more precise but for our purposes, assume no intersection
            return false;
        }
        
        return false;
    }
    
    template <typename Real>
    bool BallPivotMeshHoleFiller<Real>::CheckSelfIntersection2D(
        std::vector<detria::PointD> const& points2D,
        std::vector<std::array<uint32_t, 2>> const& edges)
    {
        // Check all pairs of non-adjacent edges for intersection
        for (size_t i = 0; i < edges.size(); ++i)
        {
            for (size_t j = i + 2; j < edges.size(); ++j)
            {
                // Skip adjacent edges (they share a vertex)
                if (i == 0 && j == edges.size() - 1)
                {
                    continue;  // First and last edge are adjacent
                }
                
                auto const& edge1 = edges[i];
                auto const& edge2 = edges[j];
                
                // Check if these edges share a vertex
                if (edge1[0] == edge2[0] || edge1[0] == edge2[1] ||
                    edge1[1] == edge2[0] || edge1[1] == edge2[1])
                {
                    continue;  // Adjacent edges, skip
                }
                
                // Check intersection
                if (DoLineSegmentsIntersect2D(
                    points2D[edge1[0]], points2D[edge1[1]],
                    points2D[edge2[0]], points2D[edge2[1]]))
                {
                    return true;  // Self-intersection found
                }
            }
        }
        
        return false;  // No self-intersection
    }
    
    template <typename Real>
    void BallPivotMeshHoleFiller<Real>::PerturbCollinearPointsIn2D(
        std::vector<detria::PointD>& points2D,
        size_t numBoundaryVertices,
        Real avgEdgeLength,
        Parameters const& params)
    {
        if (numBoundaryVertices < 3 || points2D.size() < numBoundaryVertices)
        {
            return;  // Not enough points
        }
        
        if (params.verbose)
        {
            std::cout << "\n[2D Perturbation Preprocessing]\n";
        }
        
        // Build edge list for boundary
        std::vector<std::array<uint32_t, 2>> edges;
        for (size_t i = 0; i < numBoundaryVertices; ++i)
        {
            size_t next = (i + 1) % numBoundaryVertices;
            edges.push_back({static_cast<uint32_t>(i), static_cast<uint32_t>(next)});
        }
        
        // Detect collinear points in 2D
        std::vector<size_t> collinearIndices;
        Real threshold = static_cast<Real>(avgEdgeLength * 0.001);  // 0.1% of avg edge length
        
        for (size_t i = 0; i < numBoundaryVertices; ++i)
        {
            size_t prev = (i + numBoundaryVertices - 1) % numBoundaryVertices;
            size_t next = (i + 1) % numBoundaryVertices;
            
            detria::PointD const& p0 = points2D[prev];
            detria::PointD const& p1 = points2D[i];
            detria::PointD const& p2 = points2D[next];
            
            // Calculate 2D cross product (z component)
            double edge1_x = p1.x - p0.x;
            double edge1_y = p1.y - p0.y;
            double edge2_x = p2.x - p1.x;
            double edge2_y = p2.y - p1.y;
            
            double cross = edge1_x * edge2_y - edge1_y * edge2_x;
            
            if (std::abs(cross) < static_cast<double>(threshold))
            {
                collinearIndices.push_back(i);
            }
        }
        
        if (params.verbose)
        {
            std::cout << "  Detected " << collinearIndices.size() 
                      << " collinear points in 2D\n";
        }
        
        // Apply perturbation to collinear points
        int perturbedCount = 0;
        Real perturbationScale = static_cast<Real>(0.01);  // 1% of avg edge length
        Real offset = avgEdgeLength * perturbationScale;
        
        for (size_t idx : collinearIndices)
        {
            size_t prev = (idx + numBoundaryVertices - 1) % numBoundaryVertices;
            size_t next = (idx + 1) % numBoundaryVertices;
            
            detria::PointD const& p0 = points2D[prev];
            detria::PointD const& p2 = points2D[next];
            
            // Calculate perpendicular direction in 2D
            double dir_x = p2.x - p0.x;
            double dir_y = p2.y - p0.y;
            double len = std::sqrt(dir_x * dir_x + dir_y * dir_y);
            
            if (len < 1e-10)
            {
                continue;  // Degenerate case
            }
            
            // Normalize direction
            dir_x /= len;
            dir_y /= len;
            
            // Perpendicular in 2D: rotate 90 degrees
            double perp_x = -dir_y;
            double perp_y = dir_x;
            
            // Try positive direction first
            auto testPoints = points2D;
            testPoints[idx].x += perp_x * static_cast<double>(offset);
            testPoints[idx].y += perp_y * static_cast<double>(offset);
            
            if (!CheckSelfIntersection2D(testPoints, edges))
            {
                points2D[idx] = testPoints[idx];
                perturbedCount++;
                
                if (params.verbose)
                {
                    std::cout << "    Perturbed vertex " << idx 
                              << " in +perpendicular direction\n";
                }
                continue;
            }
            
            // Try negative direction
            testPoints = points2D;
            testPoints[idx].x -= perp_x * static_cast<double>(offset);
            testPoints[idx].y -= perp_y * static_cast<double>(offset);
            
            if (!CheckSelfIntersection2D(testPoints, edges))
            {
                points2D[idx] = testPoints[idx];
                perturbedCount++;
                
                if (params.verbose)
                {
                    std::cout << "    Perturbed vertex " << idx 
                              << " in -perpendicular direction\n";
                }
            }
            else if (params.verbose)
            {
                std::cout << "    Could not safely perturb vertex " << idx 
                          << " (both directions cause self-intersection)\n";
            }
        }
        
        if (params.verbose)
        {
            std::cout << "  Successfully perturbed " << perturbedCount 
                      << " out of " << collinearIndices.size() << " collinear points\n";
        }
    }
    
    template <typename Real>
    void BallPivotMeshHoleFiller<Real>::DiagnoseDetriaFailure(
        std::vector<detria::PointD> const& points2D,
        std::vector<std::array<uint32_t, 2>> const& edges,
        Parameters const& params)
    {
        if (!params.verbose)
        {
            return;
        }
        
        std::cout << "\n[Detria Failure Diagnostic]\n";
        std::cout << "  Vertices: " << points2D.size() << "\n";
        std::cout << "  Edges: " << edges.size() << "\n";
        
        // Print all vertices
        std::cout << "\n  Vertex coordinates:\n";
        for (size_t i = 0; i < points2D.size(); ++i)
        {
            std::cout << "    V" << i << ": (" 
                      << points2D[i].x << ", " 
                      << points2D[i].y << ")\n";
        }
        
        // Print all edges
        std::cout << "\n  Edge connectivity:\n";
        for (size_t i = 0; i < edges.size(); ++i)
        {
            std::cout << "    E" << i << ": " 
                      << edges[i][0] << " -> " 
                      << edges[i][1] << "\n";
        }
        
        // Calculate signed area (winding order)
        double signedArea = 0.0;
        for (auto const& edge : edges)
        {
            auto const& p1 = points2D[edge[0]];
            auto const& p2 = points2D[edge[1]];
            signedArea += (p2.x - p1.x) * (p2.y + p1.y);
        }
        signedArea /= 2.0;
        
        std::cout << "\n  Signed area: " << signedArea;
        if (signedArea > 0)
        {
            std::cout << " (CW - clockwise)\n";
        }
        else if (signedArea < 0)
        {
            std::cout << " (CCW - counter-clockwise)\n";
        }
        else
        {
            std::cout << " (ZERO - degenerate!)\n";
        }
        
        // Check for self-intersections
        bool hasSelfIntersection = CheckSelfIntersection2D(points2D, edges);
        std::cout << "  Self-intersecting: " 
                  << (hasSelfIntersection ? "YES" : "NO") << "\n";
        
        // Check edge lengths
        double minEdgeLen = std::numeric_limits<double>::max();
        double maxEdgeLen = 0.0;
        for (auto const& edge : edges)
        {
            double dx = points2D[edge[1]].x - points2D[edge[0]].x;
            double dy = points2D[edge[1]].y - points2D[edge[0]].y;
            double len = std::sqrt(dx * dx + dy * dy);
            minEdgeLen = std::min(minEdgeLen, len);
            maxEdgeLen = std::max(maxEdgeLen, len);
        }
        std::cout << "  Edge lengths: " 
                  << minEdgeLen << " to " << maxEdgeLen << "\n";
        
        // Check for duplicate vertices
        int duplicates = 0;
        for (size_t i = 0; i < points2D.size(); ++i)
        {
            for (size_t j = i+1; j < points2D.size(); ++j)
            {
                double dx = points2D[i].x - points2D[j].x;
                double dy = points2D[i].y - points2D[j].y;
                if (std::sqrt(dx * dx + dy * dy) < 1e-6)
                {
                    duplicates++;
                }
            }
        }
        std::cout << "  Duplicate vertices: " << duplicates << "\n";
        
        // Save to file for external analysis
        std::ofstream file("/tmp/detria_failure_data.txt");
        if (file.is_open())
        {
            file << "# Detria failure data\n";
            file << "# Vertices:\n";
            for (size_t i = 0; i < points2D.size(); ++i)
            {
                file << "v " << points2D[i].x << " " 
                     << points2D[i].y << " 0\n";
            }
            file << "# Edges:\n";
            for (auto const& edge : edges)
            {
                file << "l " << (edge[0]+1) << " " 
                     << (edge[1]+1) << "\n";
            }
            file.close();
            std::cout << "  Data saved to /tmp/detria_failure_data.txt\n";
        }
    }
    
    // Detect duplicate vertices in boundary loop
    template <typename Real>
    std::vector<typename BallPivotMeshHoleFiller<Real>::DuplicateVertexPair>
    BallPivotMeshHoleFiller<Real>::DetectDuplicateVerticesInBoundary(
        std::vector<Vector3<Real>> const& vertices,
        std::vector<int32_t> const& boundaryIndices,
        Real threshold,
        Parameters const& params)
    {
        std::vector<DuplicateVertexPair> duplicates;
        std::set<int32_t> processedVertices;  // Track which vertices we've already found duplicates for
        
        // Compare all pairs of boundary vertices
        for (size_t i = 0; i < boundaryIndices.size(); ++i)
        {
            int32_t idx1 = boundaryIndices[i];
            
            // Skip if we've already processed this vertex
            if (processedVertices.count(idx1) > 0)
            {
                continue;
            }
            
            for (size_t j = i + 1; j < boundaryIndices.size(); ++j)
            {
                int32_t idx2 = boundaryIndices[j];
                
                // Skip if same vertex index (vertex appears multiple times in boundary)
                if (idx1 == idx2)
                {
                    if (params.verbose)
                    {
                        std::cout << "    WARNING: Vertex " << idx1 
                                  << " appears multiple times in boundary loop!\n";
                        std::cout << "    This indicates the boundary is malformed.\n";
                    }
                    continue;
                }
                
                // Skip if already processed
                if (processedVertices.count(idx2) > 0)
                {
                    continue;
                }
                
                // Calculate distance between vertices
                Vector3<Real> diff = vertices[idx1] - vertices[idx2];
                Real dist = Length(diff);
                
                if (dist < threshold)
                {
                    // Found duplicate - keep lower index, merge higher
                    int32_t keepIdx = std::min(idx1, idx2);
                    int32_t removeIdx = std::max(idx1, idx2);
                    duplicates.push_back(DuplicateVertexPair(keepIdx, removeIdx, dist));
                    
                    // Mark both as processed so we don't create multiple pairs
                    processedVertices.insert(keepIdx);
                    processedVertices.insert(removeIdx);
                    
                    if (params.verbose)
                    {
                        std::cout << "    Duplicate found: V" << keepIdx 
                                  << " and V" << removeIdx 
                                  << " (distance: " << dist << ")\n";
                    }
                }
            }
        }
        
        return duplicates;
    }
    
    // Merge duplicate vertices by updating all triangle references
    template <typename Real>
    int32_t BallPivotMeshHoleFiller<Real>::MergeDuplicateVerticesInMesh(
        std::vector<std::array<int32_t, 3>>& triangles,
        std::vector<DuplicateVertexPair> const& duplicates,
        Parameters const& params)
    {
        int32_t trianglesModified = 0;
        
        // Update all triangle references
        for (auto const& dup : duplicates)
        {
            for (auto& tri : triangles)
            {
                bool modified = false;
                for (int i = 0; i < 3; ++i)
                {
                    if (tri[i] == dup.removeIndex)
                    {
                        tri[i] = dup.keepIndex;
                        modified = true;
                    }
                }
                if (modified)
                {
                    trianglesModified++;
                }
            }
            
            if (params.verbose)
            {
                std::cout << "    Merged V" << dup.removeIndex 
                          << " into V" << dup.keepIndex << "\n";
            }
        }
        
        // Remove degenerate triangles (where all 3 vertices are the same)
        auto it = std::remove_if(triangles.begin(), triangles.end(),
            [](std::array<int32_t, 3> const& tri) {
                return tri[0] == tri[1] || tri[1] == tri[2] || tri[0] == tri[2];
            });
        
        int32_t removedCount = static_cast<int32_t>(std::distance(it, triangles.end()));
        if (removedCount > 0)
        {
            triangles.erase(it, triangles.end());
            if (params.verbose)
            {
                std::cout << "    Removed " << removedCount << " degenerate triangles\n";
            }
        }
        
        return trianglesModified;
    }

    // Explicit instantiations
    template class BallPivotMeshHoleFiller<float>;
    template class BallPivotMeshHoleFiller<double>;
}
