// David Eberly, Geometric Tools, Redmond WA 98052
// Copyright (c) 1998-2026
// Distributed under the Boost Software License, Version 1.0.
// https://www.boost.org/LICENSE_1_0.txt
// https://www.geometrictools.com/License/Boost/LICENSE_1_0.txt
// File Version: 8.0.2026.02.13
//
// Implementation of BallPivotReconstruction

#include <GTE/Mathematics/BallPivotReconstruction.h>
#include <iostream>
#include <queue>
#include <unordered_map>
#include <unordered_set>

namespace gte
{
    template <typename Real>
    bool BallPivotReconstruction<Real>::Reconstruct(
        std::vector<Vector3<Real>> const& points,
        std::vector<Vector3<Real>> const& normals,
        std::vector<Vector3<Real>>& outVertices,
        std::vector<std::array<int32_t, 3>>& outTriangles,
        Parameters const& params)
    {
        if (points.empty() || normals.empty() || points.size() != normals.size())
        {
            return false;
        }
        
        // Initialize reconstruction state
        ReconstructionState state;
        InitializeVertices(points, normals, state);
        
        // Build nearest neighbor query structure
        std::vector<PositionSite<3, Real>> sites;
        sites.reserve(points.size());
        for (auto const& p : points)
        {
            sites.push_back(PositionSite<3, Real>(p));
        }
        
        int32_t maxLeafSize = 10;
        int32_t maxLevel = 20;
        NearestNeighborQuery<3, Real, PositionSite<3, Real>> nnQuery(sites, maxLeafSize, maxLevel);
        
        // Compute average spacing for heuristics
        state.avgSpacing = ComputeAverageSpacingInternal(points, nnQuery);
        
        if (params.verbose)
        {
            std::cout << "[BallPivot] Starting reconstruction: " << points.size() 
                      << " points, avg spacing = " << state.avgSpacing << "\n";
        }
        
        // Determine radii to use
        std::vector<Real> radii = params.radii;
        if (radii.empty())
        {
            radii = EstimateRadii(points);
            if (params.verbose)
            {
                std::cout << "[BallPivot] Auto radii:";
                for (auto r : radii)
                {
                    std::cout << " " << r;
                }
                std::cout << "\n";
            }
        }
        
        // Process each radius in sequence
        for (auto radius : radii)
        {
            if (radius <= static_cast<Real>(0))
            {
                continue;
            }
            
            if (params.verbose)
            {
                std::cout << "[BallPivot] Processing radius = " << radius << "\n";
            }
            
            // Step 1: Update border edges that become valid at this radius
            UpdateBorderEdges(state, radius, nnQuery, params);
            
            // Step 2: Classify border loops and selectively reopen edges
            ClassifyAndReopenBorderLoops(state, radius, nnQuery, params);
            
            // Step 3: If no front edges, find a seed triangle
            if (state.frontEdges.empty())
            {
                FindSeed(state, radius, nnQuery, params);
            }
            else
            {
                // Step 4: Expand existing front
                ExpandFront(state, radius, nnQuery, params);
            }
            
            if (params.verbose)
            {
                std::cout << "[BallPivot] After radius " << radius << ": "
                          << state.outputTriangles.size() << " triangles, "
                          << state.frontEdges.size() << " front edges, "
                          << state.borderEdges.size() << " border edges\n";
            }
        }
        
        // Final step: Ensure consistent orientation
        FinalizeOrientation(state);
        
        // Copy output
        outVertices = points;  // Use original points as vertices
        outTriangles = state.outputTriangles;
        
        if (params.verbose)
        {
            std::cout << "[BallPivot] Reconstruction complete: " 
                      << outTriangles.size() << " triangles\n";
        }
        
        return !outTriangles.empty();
    }
    
    template <typename Real>
    void BallPivotReconstruction<Real>::InitializeVertices(
        std::vector<Vector3<Real>> const& points,
        std::vector<Vector3<Real>> const& normals,
        ReconstructionState& state)
    {
        state.vertices.reserve(points.size());
        for (size_t i = 0; i < points.size(); ++i)
        {
            state.vertices.emplace_back(points[i], normals[i]);
        }
    }
    
    template <typename Real>
    Real BallPivotReconstruction<Real>::ComputeAverageSpacing(
        std::vector<Vector3<Real>> const& points)
    {
        if (points.size() < 2)
        {
            return static_cast<Real>(1);
        }
        
        std::vector<PositionSite<3, Real>> sites;
        sites.reserve(points.size());
        for (auto const& p : points)
        {
            sites.push_back(PositionSite<3, Real>(p));
        }
        
        int32_t maxLeafSize = 10;
        int32_t maxLevel = 20;
        NearestNeighborQuery<3, Real, PositionSite<3, Real>> nnQuery(sites, maxLeafSize, maxLevel);
        return ComputeAverageSpacingInternal(points, nnQuery);
    }
    
    template <typename Real>
    Real BallPivotReconstruction<Real>::ComputeAverageSpacingInternal(
        std::vector<Vector3<Real>> const& points,
        NearestNeighborQuery<3, Real, PositionSite<3, Real>>& nnQuery)
    {
        if (points.size() < 2)
        {
            return static_cast<Real>(1);
        }
        
        Real totalDistance = static_cast<Real>(0);
        int32_t numSamples = 0;
        
        // Sample up to 1000 points to estimate spacing
        size_t step = std::max<size_t>(1, points.size() / 1000);
        
        for (size_t i = 0; i < points.size(); i += step)
        {
            // Find nearest neighbors
            constexpr int32_t MaxNeighbors = 10;
            std::array<int32_t, MaxNeighbors> indices;
            
            // Use large radius to ensure we find neighbors
            Real searchRadius = static_cast<Real>(1000);  // Very large
            int32_t numFound = nnQuery.template FindNeighbors<MaxNeighbors>(
                points[i], searchRadius, indices);
            
            if (numFound >= 2)
            {
                // Find nearest neighbor that's not self
                Real minDist = std::numeric_limits<Real>::max();
                for (int32_t j = 0; j < numFound; ++j)
                {
                    if (indices[j] == static_cast<int32_t>(i))
                    {
                        continue;  // Skip self
                    }
                    
                    Vector3<Real> diff = points[indices[j]] - points[i];
                    Real dist = Length(diff);
                    if (dist > static_cast<Real>(0) && dist < minDist)
                    {
                        minDist = dist;
                    }
                }
                
                if (minDist < std::numeric_limits<Real>::max())
                {
                    totalDistance += minDist;
                    ++numSamples;
                }
            }
        }
        
        return numSamples > 0 ? totalDistance / static_cast<Real>(numSamples) : static_cast<Real>(1);
    }
    
    template <typename Real>
    std::vector<Real> BallPivotReconstruction<Real>::EstimateRadii(
        std::vector<Vector3<Real>> const& points)
    {
        if (points.size() < 2)
        {
            return {static_cast<Real>(1.5)};
        }
        
        // Compute nearest neighbor distances
        std::vector<PositionSite<3, Real>> sites;
        sites.reserve(points.size());
        for (auto const& p : points)
        {
            sites.push_back(PositionSite<3, Real>(p));
        }
        
        int32_t maxLeafSize = 10;
        int32_t maxLevel = 20;
        NearestNeighborQuery<3, Real, PositionSite<3, Real>> nnQuery(sites, maxLeafSize, maxLevel);
        
        std::vector<Real> distances;
        distances.reserve(points.size());
        
        for (size_t i = 0; i < points.size(); ++i)
        {
            constexpr int32_t MaxNeighbors = 10;
            std::array<int32_t, MaxNeighbors> indices;
            Real searchRadius = static_cast<Real>(1000);  // Very large
            int32_t numFound = nnQuery.template FindNeighbors<MaxNeighbors>(
                points[i], searchRadius, indices);
                
            if (numFound >= 2)
            {
                // Find nearest neighbor that's not self
                Real minDist = std::numeric_limits<Real>::max();
                for (int32_t j = 0; j < numFound; ++j)
                {
                    if (indices[j] == static_cast<int32_t>(i))
                    {
                        continue;  // Skip self
                    }
                    
                    Vector3<Real> diff = points[indices[j]] - points[i];
                    Real dist = Length(diff);
                    if (dist > static_cast<Real>(0) && dist < minDist)
                    {
                        minDist = dist;
                    }
                }
                
                if (minDist < std::numeric_limits<Real>::max())
                {
                    distances.push_back(minDist);
                }
            }
        }
        
        if (distances.empty())
        {
            return {static_cast<Real>(1.5)};
        }
        
        // Compute statistics
        std::sort(distances.begin(), distances.end());
        
        Real sum = static_cast<Real>(0);
        for (auto d : distances)
        {
            sum += d;
        }
        Real avg = sum / static_cast<Real>(distances.size());
        
        size_t medianIdx = distances.size() / 2;
        Real median = distances[medianIdx];
        
        size_t p90Idx = static_cast<size_t>(0.9 * (distances.size() - 1));
        Real p90 = distances[p90Idx];
        
        // Generate 3 radii based on statistics
        Real r0 = static_cast<Real>(0.9) * median;
        Real r1 = static_cast<Real>(1.2) * avg;
        Real r2 = std::min(static_cast<Real>(2.5) * avg, static_cast<Real>(1.1) * p90);
        
        // Ensure increasing order and positive values
        if (r1 <= r0)
        {
            r1 = r0 * static_cast<Real>(1.1);
        }
        if (r2 <= r1)
        {
            r2 = r1 * static_cast<Real>(1.1);
        }
        
        if (r0 <= static_cast<Real>(0))
        {
            r0 = avg * static_cast<Real>(0.8);
        }
        if (r1 <= static_cast<Real>(0))
        {
            r1 = avg * static_cast<Real>(1.2);
        }
        if (r2 <= static_cast<Real>(0))
        {
            r2 = avg * static_cast<Real>(2.0);
        }
        
        return {r0, r1, r2};
    }
    
    template <typename Real>
    bool BallPivotReconstruction<Real>::ComputeBallCenter(
        Vector3<Real> const& p0,
        Vector3<Real> const& p1,
        Vector3<Real> const& p2,
        Vector3<Real> const& n0,
        Vector3<Real> const& n1,
        Vector3<Real> const& n2,
        Real radius,
        Vector3<Real> const* midpoint,
        Vector3<Real> const* preferredCenter,
        Vector3<Real>& center)
    {
        // Compute triangle edges
        Vector3<Real> e01 = p1 - p0;
        Vector3<Real> e02 = p2 - p0;
        
        // Triangle normal
        Vector3<Real> ntri = Cross(e01, e02);
        Real ntriLenSq = Dot(ntri, ntri);
        
        if (ntriLenSq < static_cast<Real>(1e-16))
        {
            // Degenerate triangle
            return false;
        }
        
        // Squared side lengths
        Vector3<Real> side12 = p1 - p2;
        Vector3<Real> side20 = p2 - p0;
        Vector3<Real> side01 = p0 - p1;
        
        Real a = Dot(side12, side12);  // |p1-p2|²
        Real b = Dot(side20, side20);  // |p2-p0|²
        Real c = Dot(side01, side01);  // |p0-p1|²
        
        // Barycentric weights for circumcenter
        Real abg = a*(b + c - a) + b*(a + c - b) + c*(a + b - c);
        
        if (std::abs(abg) < static_cast<Real>(1e-16))
        {
            // Degenerate configuration
            return false;
        }
        
        Real alpha = a * (b + c - a) / abg;
        Real beta  = b * (a + c - b) / abg;
        Real gamma = c * (a + b - c) / abg;
        
        // Circumcenter
        Vector3<Real> circumcenter = alpha * p0 + beta * p1 + gamma * p2;
        
        // Circumradius²
        Real la = std::sqrt(a);
        Real lb = std::sqrt(b);
        Real lc = std::sqrt(c);
        Real denom = (la + lb + lc) * (lb + lc - la) * (lc + la - lb) * (la + lb - lc);
        
        if (std::abs(denom) < static_cast<Real>(1e-16))
        {
            // Degenerate configuration
            return false;
        }
        
        Real r2 = (a * b * c) / denom;
        
        // Check if ball can contain this triangle
        Real h2 = radius * radius - r2;
        
        if (h2 < static_cast<Real>(0))
        {
            // Ball too small for this triangle
            return false;
        }
        
        // Unit triangle normal
        Vector3<Real> trN = ntri;
        Normalize(trN);
        
        // Two possible ball centers (offset ±h from circumcenter along normal)
        Real h = std::sqrt(h2);
        Vector3<Real> cpos = circumcenter + h * trN;
        Vector3<Real> cneg = circumcenter - h * trN;
        
        // Choose based on preference
        if (midpoint && preferredCenter)
        {
            // Choose center whose direction from midpoint aligns best with preferred direction
            Vector3<Real> vPref = *preferredCenter - *midpoint;
            Vector3<Real> vPos = cpos - *midpoint;
            Vector3<Real> vNeg = cneg - *midpoint;
            
            Real lenPref = Length(vPref);
            Real lenPos = Length(vPos);
            Real lenNeg = Length(vNeg);
            
            if (lenPref > static_cast<Real>(0))
            {
                vPref /= lenPref;
            }
            if (lenPos > static_cast<Real>(0))
            {
                vPos /= lenPos;
            }
            if (lenNeg > static_cast<Real>(0))
            {
                vNeg /= lenNeg;
            }
            
            Real dotPos = Dot(vPref, vPos);
            Real dotNeg = Dot(vPref, vNeg);
            
            center = (dotPos >= dotNeg) ? cpos : cneg;
        }
        else
        {
            // Fall back to summed normals
            Vector3<Real> nsum = n0 + n1 + n2;
            Real nsumLen = Length(nsum);
            
            if (nsumLen > static_cast<Real>(0))
            {
                nsum /= nsumLen;
            }
            
            // Prefer center whose offset aligns with normal sum
            center = (Dot(trN, nsum) >= static_cast<Real>(0)) ? cpos : cneg;
        }
        
        return true;
    }
    
    template <typename Real>
    bool BallPivotReconstruction<Real>::AreNormalsCompatible(
        Vector3<Real> const& n0,
        Vector3<Real> const& n1,
        Vector3<Real> const& n2,
        Vector3<Real> const& p0,
        Vector3<Real> const& p1,
        Vector3<Real> const& p2,
        Real cosThreshold)
    {
        // Compute sum of normals
        Vector3<Real> nsum = n0 + n1 + n2;
        Real nsumLen = Length(nsum);
        
        if (nsumLen <= static_cast<Real>(1e-20))
        {
            // Degenerate normals, don't block
            return true;
        }
        
        nsum /= nsumLen;
        
        // Compute face normal
        Vector3<Real> e1 = p1 - p0;
        Vector3<Real> e2 = p2 - p0;
        Vector3<Real> faceNormal = Cross(e1, e2);
        Real faceLen = Length(faceNormal);
        
        if (faceLen > static_cast<Real>(0))
        {
            faceNormal /= faceLen;
        }
        else
        {
            return false;  // Degenerate triangle
        }
        
        // Check agreement
        Real dotProduct = std::abs(Dot(faceNormal, nsum));
        return dotProduct >= cosThreshold;
    }
    
    template <typename Real>
    void BallPivotReconstruction<Real>::FindSeed(
        ReconstructionState& state,
        Real radius,
        NearestNeighborQuery<3, Real, PositionSite<3, Real>>& nnQuery,
        Parameters const& params)
    {
        // Try to find a seed triangle from orphan vertices
        for (size_t i = 0; i < state.vertices.size(); ++i)
        {
            if (state.vertices[i].type != VertexType::Orphan)
            {
                continue;
            }
            
            // Search for neighbors
            constexpr int32_t MaxNeighbors = 256;  // Reasonable limit for seed search
            std::array<int32_t, MaxNeighbors> neighborIndices;
            int32_t numFound = nnQuery.template FindNeighbors<MaxNeighbors>(
                state.vertices[i].position, static_cast<Real>(2) * radius, neighborIndices);
            
            if (numFound < 3)
            {
                continue;
            }
            
            // Try combinations of 3 vertices
            for (int32_t j = 0; j < numFound; ++j)
            {
                int32_t idx0 = neighborIndices[j];
                
                if (state.vertices[idx0].type != VertexType::Orphan || idx0 == static_cast<int32_t>(i))
                {
                    continue;
                }
                
                for (int32_t k = j + 1; k < numFound; ++k)
                {
                    int32_t idx1 = neighborIndices[k];
                    
                    if (state.vertices[idx1].type != VertexType::Orphan || idx1 == static_cast<int32_t>(i))
                    {
                        continue;
                    }
                    
                    // Check if we can form a valid seed triangle
                    int32_t v0 = static_cast<int32_t>(i);
                    int32_t v1 = idx0;
                    int32_t v2 = idx1;
                    
                    // Check normal compatibility
                    if (!AreNormalsCompatible(
                        state.vertices[v0].normal,
                        state.vertices[v1].normal,
                        state.vertices[v2].normal,
                        state.vertices[v0].position,
                        state.vertices[v1].position,
                        state.vertices[v2].position,
                        params.nsumMinDot))
                    {
                        continue;
                    }
                    
                    // Compute ball center
                    Vector3<Real> ballCenter;
                    if (!ComputeBallCenter(
                        state.vertices[v0].position,
                        state.vertices[v1].position,
                        state.vertices[v2].position,
                        state.vertices[v0].normal,
                        state.vertices[v1].normal,
                        state.vertices[v2].normal,
                        radius,
                        nullptr,
                        nullptr,
                        ballCenter))
                    {
                        continue;
                    }
                    
                    // Check if ball is empty
                    bool empty = true;
                    for (int32_t idx = 0; idx < numFound; ++idx)
                    {
                        int32_t nbIdx = neighborIndices[idx];
                        if (nbIdx == v0 || nbIdx == v1 || nbIdx == v2)
                        {
                            continue;
                        }
                        
                        Vector3<Real> diff = ballCenter - state.vertices[nbIdx].position;
                        if (Length(diff) < radius - static_cast<Real>(1e-16))
                        {
                            empty = false;
                            break;
                        }
                    }
                    
                    if (empty)
                    {
                        // Found a valid seed! Create the triangle
                        CreateFace(v0, v1, v2, ballCenter, state, params);
                        
                        if (params.verbose)
                        {
                            std::cout << "[BallPivot] Found seed triangle: (" 
                                      << v0 << "," << v1 << "," << v2 << ")\n";
                        }
                        
                        return;  // Seed found, expansion will continue from here
                    }
                }
            }
        }
    }
    
    template <typename Real>
    int32_t BallPivotReconstruction<Real>::FindEdge(
        int32_t v0,
        int32_t v1,
        ReconstructionState const& state)
    {
        // Check edges of v0
        for (auto edgeIdx : state.vertices[v0].edgeIndices)
        {
            auto const& edge = state.edges[edgeIdx];
            if ((edge.vertexA == v0 && edge.vertexB == v1) ||
                (edge.vertexA == v1 && edge.vertexB == v0))
            {
                return edgeIdx;
            }
        }
        
        return -1;  // Not found
    }
    
    template <typename Real>
    int32_t BallPivotReconstruction<Real>::GetOrCreateEdge(
        int32_t v0,
        int32_t v1,
        int32_t faceIndex,
        ReconstructionState& state)
    {
        // Try to find existing edge
        int32_t edgeIdx = FindEdge(v0, v1, state);
        
        if (edgeIdx < 0)
        {
            // Create new edge
            edgeIdx = static_cast<int32_t>(state.edges.size());
            state.edges.emplace_back(v0, v1);
            
            // Add to vertex edge lists
            state.vertices[v0].edgeIndices.insert(edgeIdx);
            state.vertices[v1].edgeIndices.insert(edgeIdx);
        }
        
        // Add face to edge
        auto& edge = state.edges[edgeIdx];
        if (edge.face0 < 0)
        {
            edge.face0 = faceIndex;
            edge.type = EdgeType::Front;
            
            // Compute orientation sign
            if (faceIndex >= 0 && faceIndex < static_cast<int32_t>(state.faces.size()))
            {
                auto const& face = state.faces[faceIndex];
                
                // Find opposite vertex
                int32_t oppVertex = -1;
                if (face.v0 != v0 && face.v0 != v1) oppVertex = face.v0;
                else if (face.v1 != v0 && face.v1 != v1) oppVertex = face.v1;
                else if (face.v2 != v0 && face.v2 != v1) oppVertex = face.v2;
                
                if (oppVertex >= 0)
                {
                    // Compute orientation
                    Vector3<Real> e = state.vertices[v1].position - state.vertices[v0].position;
                    Vector3<Real> toOpp = state.vertices[oppVertex].position - state.vertices[v0].position;
                    Vector3<Real> triNormal = Cross(e, toOpp);
                    
                    Vector3<Real> nsum = state.vertices[v0].normal + 
                                        state.vertices[v1].normal + 
                                        state.vertices[oppVertex].normal;
                    
                    edge.orientSign = (Dot(triNormal, nsum) >= static_cast<Real>(0)) ? 1 : -1;
                }
            }
        }
        else if (edge.face1 < 0)
        {
            edge.face1 = faceIndex;
            edge.type = EdgeType::Inner;
        }
        
        return edgeIdx;
    }
    
    template <typename Real>
    void BallPivotReconstruction<Real>::CreateFace(
        int32_t v0,
        int32_t v1,
        int32_t v2,
        Vector3<Real> const& ballCenter,
        ReconstructionState& state,
        Parameters const& params)
    {
        // Create face structure
        int32_t faceIdx = static_cast<int32_t>(state.faces.size());
        state.faces.emplace_back(v0, v1, v2, ballCenter);
        
        // Get or create edges
        int32_t edge0 = GetOrCreateEdge(v0, v1, faceIdx, state);
        int32_t edge1 = GetOrCreateEdge(v1, v2, faceIdx, state);
        int32_t edge2 = GetOrCreateEdge(v2, v0, faceIdx, state);
        
        // Update vertex types
        for (auto vIdx : {v0, v1, v2})
        {
            auto& vertex = state.vertices[vIdx];
            
            // Check if vertex is now inner (all edges are inner)
            bool allInner = true;
            for (auto edgeIdx : vertex.edgeIndices)
            {
                if (state.edges[edgeIdx].type != EdgeType::Inner)
                {
                    allInner = false;
                    break;
                }
            }
            
            vertex.type = allInner ? VertexType::Inner : VertexType::Front;
        }
        
        // Determine correct winding based on normals
        Vector3<Real> nsum = state.vertices[v0].normal + 
                            state.vertices[v1].normal + 
                            state.vertices[v2].normal;
        Real nsumLen = Length(nsum);
        
        if (nsumLen > static_cast<Real>(0))
        {
            nsum /= nsumLen;
        }
        
        Vector3<Real> edgeV1 = state.vertices[v1].position - state.vertices[v0].position;
        Vector3<Real> edgeV2 = state.vertices[v2].position - state.vertices[v0].position;
        Vector3<Real> faceNormal = Cross(edgeV1, edgeV2);
        
        // If face normal disagrees with nsum, flip winding
        if (nsumLen > static_cast<Real>(0) && Dot(faceNormal, nsum) < static_cast<Real>(0))
        {
            state.outputTriangles.push_back({v0, v2, v1});
            
            // Recompute normal with flipped order
            edgeV2 = state.vertices[v1].position - state.vertices[v0].position;
            edgeV1 = state.vertices[v2].position - state.vertices[v0].position;
            faceNormal = Cross(edgeV1, edgeV2);
        }
        else
        {
            state.outputTriangles.push_back({v0, v1, v2});
        }
        
        // Normalize and store face normal
        Real faceLen = Length(faceNormal);
        if (faceLen > static_cast<Real>(0))
        {
            faceNormal /= faceLen;
        }
        state.outputNormals.push_back(faceNormal);
        
        // Add front edges to front list
        for (auto edgeIdx : {edge0, edge1, edge2})
        {
            if (state.edges[edgeIdx].type == EdgeType::Front)
            {
                state.frontEdges.push_back(edgeIdx);
            }
        }
    }
    
    template <typename Real>
    int32_t BallPivotReconstruction<Real>::FindNextVertex(
        int32_t edgeIndex,
        Real radius,
        Vector3<Real>& outCenter,
        ReconstructionState const& state,
        NearestNeighborQuery<3, Real, PositionSite<3, Real>>& nnQuery,
        Parameters const& params)
    {
        auto const& edge = state.edges[edgeIndex];
        
        if (edge.face0 < 0)
        {
            return -1;  // No face, can't pivot
        }
        
        int32_t va = edge.vertexA;
        int32_t vb = edge.vertexB;
        
        // Find opposite vertex from existing face
        auto const& face = state.faces[edge.face0];
        int32_t oppVertex = -1;
        if (face.v0 != va && face.v0 != vb) oppVertex = face.v0;
        else if (face.v1 != va && face.v1 != vb) oppVertex = face.v1;
        else if (face.v2 != va && face.v2 != vb) oppVertex = face.v2;
        
        if (oppVertex < 0)
        {
            return -1;
        }
        
        // Edge midpoint
        Vector3<Real> midpoint = static_cast<Real>(0.5) * 
            (state.vertices[va].position + state.vertices[vb].position);
        
        // Previous ball center (for continuity)
        Vector3<Real> const& prevCenter = face.ballCenter;
        
        // Direction vectors for angle computation
        Vector3<Real> edgeVec = state.vertices[vb].position - state.vertices[va].position;
        Real edgeLen = Length(edgeVec);
        if (edgeLen > static_cast<Real>(0))
        {
            edgeVec /= edgeLen;
        }
        
        Vector3<Real> vA = prevCenter - midpoint;
        Real vALen = Length(vA);
        if (vALen > static_cast<Real>(0))
        {
            vA /= vALen;
        }
        
        // Search for candidates
        constexpr int32_t MaxCandidates = 512;
        std::array<int32_t, MaxCandidates> candidateIndices;
        int32_t numCandidates = nnQuery.template FindNeighbors<MaxCandidates>(
            midpoint, static_cast<Real>(2) * radius, candidateIndices);
        
        int32_t bestCandidate = -1;
        Real minAngle = static_cast<Real>(2) * static_cast<Real>(3.14159265358979323846);  // 2π
        Vector3<Real> bestCenter;
        
        for (int32_t candIdx = 0; candIdx < numCandidates; ++candIdx)
        {
            int32_t idx = candidateIndices[candIdx];
            
            if (idx == va || idx == vb || idx == oppVertex)
            {
                continue;
            }
            
            if (state.vertices[idx].type == VertexType::Inner)
            {
                continue;
            }
            
            // Check normal compatibility
            if (!AreNormalsCompatible(
                state.vertices[idx].normal,
                state.vertices[va].normal,
                state.vertices[vb].normal,
                state.vertices[idx].position,
                state.vertices[va].position,
                state.vertices[vb].position,
                params.nsumMinDot))
            {
                continue;
            }
            
            // Compute ball center with continuity preference
            Vector3<Real> newCenter;
            if (!ComputeBallCenter(
                state.vertices[va].position,
                state.vertices[vb].position,
                state.vertices[idx].position,
                state.vertices[va].normal,
                state.vertices[vb].normal,
                state.vertices[idx].normal,
                radius,
                &midpoint,
                &prevCenter,
                newCenter))
            {
                continue;
            }
            
            // Compute pivot angle
            Vector3<Real> vB = newCenter - midpoint;
            Real vBLen = Length(vB);
            if (vBLen > static_cast<Real>(0))
            {
                vB /= vBLen;
            }
            
            Real cosAngle = std::max(static_cast<Real>(-1), 
                                    std::min(static_cast<Real>(1), Dot(vA, vB)));
            Real angle = std::acos(cosAngle);
            
            // Determine sign of angle (which side of edge)
            Vector3<Real> crossVec = Cross(vA, vB);
            Real sign = Dot(crossVec, edgeVec);
            
            if (edge.orientSign < 0)
            {
                sign = -sign;
            }
            
            if (sign < static_cast<Real>(0))
            {
                angle = static_cast<Real>(2) * static_cast<Real>(3.14159265358979323846) - angle;
            }
            
            if (angle >= minAngle)
            {
                continue;
            }
            
            // Check if ball is empty
            bool empty = true;
            for (int32_t nbIdx2 = 0; nbIdx2 < numCandidates; ++nbIdx2)
            {
                int32_t nbIdx = candidateIndices[nbIdx2];
                if (nbIdx == va || nbIdx == vb || nbIdx == idx)
                {
                    continue;
                }
                
                Vector3<Real> diff = newCenter - state.vertices[nbIdx].position;
                if (Length(diff) < radius - static_cast<Real>(1e-16))
                {
                    empty = false;
                    break;
                }
            }
            
            if (empty)
            {
                minAngle = angle;
                bestCandidate = idx;
                bestCenter = newCenter;
            }
        }
        
        if (bestCandidate >= 0)
        {
            outCenter = bestCenter;
        }
        
        return bestCandidate;
    }
    
    template <typename Real>
    void BallPivotReconstruction<Real>::ExpandFront(
        ReconstructionState& state,
        Real radius,
        NearestNeighborQuery<3, Real, PositionSite<3, Real>>& nnQuery,
        Parameters const& params)
    {
        while (!state.frontEdges.empty())
        {
            int32_t edgeIdx = state.frontEdges.front();
            state.frontEdges.pop_front();
            
            if (state.edges[edgeIdx].type != EdgeType::Front)
            {
                continue;
            }
            
            Vector3<Real> ballCenter;
            int32_t nextVertex = FindNextVertex(edgeIdx, radius, ballCenter, state, nnQuery, params);
            
            if (nextVertex < 0)
            {
                // No valid candidate, mark as border
                state.edges[edgeIdx].type = EdgeType::Border;
                state.borderEdges.push_back(edgeIdx);
                continue;
            }
            
            auto const& edge = state.edges[edgeIdx];
            
            // Check if edges to candidate already exist and are not front
            int32_t edgeA = FindEdge(nextVertex, edge.vertexA, state);
            int32_t edgeB = FindEdge(nextVertex, edge.vertexB, state);
            
            if ((edgeA >= 0 && state.edges[edgeA].type != EdgeType::Front) ||
                (edgeB >= 0 && state.edges[edgeB].type != EdgeType::Front))
            {
                state.edges[edgeIdx].type = EdgeType::Border;
                state.borderEdges.push_back(edgeIdx);
                continue;
            }
            
            // Create new face
            CreateFace(edge.vertexA, edge.vertexB, nextVertex, ballCenter, state, params);
        }
    }
    
    template <typename Real>
    void BallPivotReconstruction<Real>::UpdateBorderEdges(
        ReconstructionState& state,
        Real radius,
        NearestNeighborQuery<3, Real, PositionSite<3, Real>>& nnQuery,
        Parameters const& params)
    {
        // Try to reactivate border edges that might become valid with new radius
        std::list<int32_t> newFrontEdges;
        
        for (auto it = state.borderEdges.begin(); it != state.borderEdges.end(); )
        {
            int32_t edgeIdx = *it;
            auto& edge = state.edges[edgeIdx];
            
            if (edge.face0 < 0)
            {
                ++it;
                continue;
            }
            
            auto const& face = state.faces[edge.face0];
            
            // Try to compute ball center at new radius
            Vector3<Real> ballCenter;
            if (ComputeBallCenter(
                state.vertices[face.v0].position,
                state.vertices[face.v1].position,
                state.vertices[face.v2].position,
                state.vertices[face.v0].normal,
                state.vertices[face.v1].normal,
                state.vertices[face.v2].normal,
                radius,
                nullptr,
                nullptr,
                ballCenter))
            {
                // Check if ball is empty
                constexpr int32_t MaxNeighbors = 256;
                std::array<int32_t, MaxNeighbors> neighborIndices;
                int32_t numNeighbors = nnQuery.template FindNeighbors<MaxNeighbors>(
                    ballCenter, radius, neighborIndices);
                
                bool empty = true;
                for (int32_t idx = 0; idx < numNeighbors; ++idx)
                {
                    int32_t nbIdx = neighborIndices[idx];
                    if (nbIdx != face.v0 && nbIdx != face.v1 && nbIdx != face.v2)
                    {
                        Vector3<Real> diff = ballCenter - state.vertices[nbIdx].position;
                        if (Length(diff) < radius - static_cast<Real>(1e-16))
                        {
                            empty = false;
                            break;
                        }
                    }
                }
                
                if (empty)
                {
                    // Reactivate this edge
                    edge.type = EdgeType::Front;
                    newFrontEdges.push_back(edgeIdx);
                    it = state.borderEdges.erase(it);
                    continue;
                }
            }
            
            ++it;
        }
        
        // Add reactivated edges to front
        for (auto edgeIdx : newFrontEdges)
        {
            state.frontEdges.push_back(edgeIdx);
        }
    }
    
    template <typename Real>
    void BallPivotReconstruction<Real>::ClassifyAndReopenBorderLoops(
        ReconstructionState& state,
        Real radius,
        NearestNeighborQuery<3, Real, PositionSite<3, Real>>& nnQuery,
        Parameters const& params)
    {
        // Simplified version: skip complex loop classification for MVP
        // This can be enhanced later with full heuristics
    }
    
    template <typename Real>
    void BallPivotReconstruction<Real>::FinalizeOrientation(ReconstructionState& state)
    {
        if (state.outputTriangles.empty())
        {
            return;
        }
        
        // Build edge adjacency map
        std::map<std::pair<int32_t, int32_t>, std::vector<int32_t>> edgeToFaces;
        
        for (size_t i = 0; i < state.outputTriangles.size(); ++i)
        {
            auto const& tri = state.outputTriangles[i];
            
            for (int j = 0; j < 3; ++j)
            {
                int32_t v0 = tri[j];
                int32_t v1 = tri[(j + 1) % 3];
                int32_t u = std::min(v0, v1);
                int32_t v = std::max(v0, v1);
                
                edgeToFaces[{u, v}].push_back(static_cast<int32_t>(i));
            }
        }
        
        // BFS to ensure consistent orientation per component
        std::vector<bool> visited(state.outputTriangles.size(), false);
        
        for (size_t seed = 0; seed < state.outputTriangles.size(); ++seed)
        {
            if (visited[seed])
            {
                continue;
            }
            
            std::queue<int32_t> queue;
            queue.push(static_cast<int32_t>(seed));
            visited[seed] = true;
            
            while (!queue.empty())
            {
                int32_t faceIdx = queue.front();
                queue.pop();
                
                auto const& tri = state.outputTriangles[faceIdx];
                
                // Check all edges
                for (int j = 0; j < 3; ++j)
                {
                    int32_t v0 = tri[j];
                    int32_t v1 = tri[(j + 1) % 3];
                    int32_t u = std::min(v0, v1);
                    int32_t v = std::max(v0, v1);
                    
                    auto const& adjacentFaces = edgeToFaces[{u, v}];
                    
                    for (auto adjIdx : adjacentFaces)
                    {
                        if (adjIdx == faceIdx || visited[adjIdx])
                        {
                            continue;
                        }
                        
                        // Check if normals agree
                        Real dotProduct = Dot(state.outputNormals[faceIdx], 
                                            state.outputNormals[adjIdx]);
                        
                        if (dotProduct < static_cast<Real>(0))
                        {
                            // Flip adjacent face
                            std::swap(state.outputTriangles[adjIdx][1], 
                                     state.outputTriangles[adjIdx][2]);
                            state.outputNormals[adjIdx] = -state.outputNormals[adjIdx];
                        }
                        
                        visited[adjIdx] = true;
                        queue.push(adjIdx);
                    }
                }
            }
        }
    }
    
    // Explicit template instantiations
    template class BallPivotReconstruction<float>;
    template class BallPivotReconstruction<double>;
}
