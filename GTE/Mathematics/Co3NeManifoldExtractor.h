// David Eberly, Geometric Tools, Redmond WA 98052
// Copyright (c) 1998-2026
// Distributed under the Boost Software License, Version 1.0.
// https://www.boost.org/LICENSE_1_0.txt
// https://www.geometrictools.com/License/Boost/LICENSE_1_0.txt
// File Version: 8.0.2026.02.12
//
// Co3Ne Manifold Extraction - Full implementation from Geogram
//
// Original Geogram Source:
// - geogram/src/lib/geogram/points/co3ne.cpp (Co3NeManifoldExtraction class)
// - Lines 124-1097 (~1100 lines)
// - https://github.com/BrunoLevy/geogram (commit f5abd69)
// License: BSD 3-Clause (Inria) - Compatible with Boost
// Copyright (c) 2000-2022 Inria
//
// This implements the full sophisticated manifold extraction algorithm from
// Geogram's Co3Ne, including:
// - Corner-based topology tracking (halfedge-like data structure)
// - Connected component analysis with orientation tracking
// - Moebius strip detection
// - Incremental triangle insertion with 4 validation tests
// - Rollback mechanism for failed insertions
//
// Adapted for Geometric Tools Engine:
// - Uses std::vector and std::array instead of GEO::Mesh
// - Uses int32_t for indices
// - Removed Geogram's Logger and command-line dependencies
// - Added parameter-based configuration

#ifndef GTE_MATHEMATICS_CO3NE_MANIFOLD_EXTRACTOR_H
#define GTE_MATHEMATICS_CO3NE_MANIFOLD_EXTRACTOR_H

#include <GTE/Mathematics/Logger.h>
#include <GTE/Mathematics/Vector3.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <stack>
#include <vector>

namespace gte
{
    template <typename Real>
    class Co3NeManifoldExtractor
    {
    public:
        static constexpr int32_t NO_CORNER = -1;
        static constexpr int32_t NO_FACET = -1;
        static constexpr int32_t NO_CNX = -1;
        
        struct Parameters
        {
            bool strictMode;              // Strict manifold extraction (more conservative)
            int32_t maxIterations;        // Max iterations for incremental insertion
            bool verbose;                 // Enable verbose output
            
            Parameters()
                : strictMode(false)
                , maxIterations(50)
                , verbose(false)
            {
            }
        };
        
        // Constructor
        Co3NeManifoldExtractor(
            std::vector<Vector3<Real>> const& points,
            Parameters const& params = Parameters())
            : mPoints(points)
            , mParams(params)
        {
        }
        
        // Main extraction method
        // Takes candidate triangles and extracts a manifold subset
        void Extract(
            std::vector<std::array<int32_t, 3>> const& goodTriangles,
            std::vector<std::array<int32_t, 3>> const& candidateTriangles,
            std::vector<std::array<int32_t, 3>>& outTriangles)
        {
            // Initialize with good triangles
            InitializeWithTriangles(goodTriangles);
            
            // Initialize topology structures
            InitAndRemoveNonManifoldEdges();
            
            // Initialize connected components
            InitConnectedComponents();
            
            // Add candidate triangles incrementally
            if (!candidateTriangles.empty())
            {
                AddTriangles(candidateTriangles);
            }
            
            // Extract result
            GetResult(outTriangles);
        }
        
    private:
        // ===== DATA STRUCTURES =====
        
        std::vector<Vector3<Real>> const& mPoints;
        Parameters mParams;
        
        // The mesh being built
        std::vector<std::array<int32_t, 3>> mTriangles;
        
        // For each corner, next_c_around_v_[c] chains the circular list
        // of corners incident to the same vertex as c
        std::vector<int32_t> mNextCornerAroundVertex;
        
        // For each vertex v, v2c_[v] contains a corner incident to v,
        // or NO_CORNER if v is isolated
        std::vector<int32_t> mVertexToCorner;
        
        // For each triangle t, cnx_[t] contains the index of the
        // connected component of the mesh incident to t
        std::vector<int32_t> mConnectedComponent;
        
        // For each connected component C, cnx_size_[C] contains
        // the number of facets in C
        std::vector<int32_t> mComponentSize;
        
        // Triangle adjacency: for each corner c, the adjacent triangle
        // (or NO_FACET if on boundary)
        std::vector<int32_t> mCornerToAdjacentFacet;
        
        // ===== INITIALIZATION =====
        
        void InitializeWithTriangles(std::vector<std::array<int32_t, 3>> const& triangles)
        {
            mTriangles = triangles;
            
            // Size the adjacency array (3 corners per triangle)
            mCornerToAdjacentFacet.assign(mTriangles.size() * 3, NO_FACET);
            
            // Find max vertex index to size vertex arrays
            int32_t maxVertex = 0;
            for (auto const& tri : mTriangles)
            {
                for (int i = 0; i < 3; ++i)
                {
                    maxVertex = std::max(maxVertex, tri[i]);
                }
            }
            
            mVertexToCorner.assign(maxVertex + 1, NO_CORNER);
            mNextCornerAroundVertex.assign(mTriangles.size() * 3, NO_CORNER);
        }
        
        void InitAndRemoveNonManifoldEdges()
        {
            // Build topology for all triangles
            for (int32_t t = 0; t < static_cast<int32_t>(mTriangles.size()); ++t)
            {
                Insert(t);
            }
            
            // Find non-manifold edges and mark triangles for removal
            std::vector<bool> removeTriangle(mTriangles.size(), false);
            int32_t nbNonManifold = 0;
            
            for (int32_t t = 0; t < static_cast<int32_t>(mTriangles.size()); ++t)
            {
                if (!Connect(t))
                {
                    removeTriangle[t] = true;
                    ++nbNonManifold;
                }
            }
            
            // Reorient mesh and remove Moebius configurations
            ReorientMesh(removeTriangle);
            
            if (nbNonManifold > 0 || HasMarkedTriangles(removeTriangle))
            {
                // Remove marked triangles
                DeleteMarkedTriangles(removeTriangle);
                
                // Rebuild topology structures
                mNextCornerAroundVertex.assign(mTriangles.size() * 3, NO_CORNER);
                mVertexToCorner.assign(mVertexToCorner.size(), NO_CORNER);
                mCornerToAdjacentFacet.assign(mTriangles.size() * 3, NO_FACET);
                
                for (int32_t t = 0; t < static_cast<int32_t>(mTriangles.size()); ++t)
                {
                    Insert(t);
                }
            }
        }
        
        void InitConnectedComponents()
        {
            mConnectedComponent.assign(mTriangles.size(), NO_CNX);
            mComponentSize.clear();
            
            for (int32_t t = 0; t < static_cast<int32_t>(mTriangles.size()); ++t)
            {
                if (mConnectedComponent[t] == NO_CNX)
                {
                    int32_t cnxId = static_cast<int32_t>(mComponentSize.size());
                    int32_t nb = 0;
                    
                    std::stack<int32_t> S;
                    S.push(t);
                    mConnectedComponent[t] = cnxId;
                    
                    while (!S.empty())
                    {
                        int32_t t2 = S.top();
                        S.pop();
                        ++nb;
                        
                        // Traverse neighbors
                        for (int i = 0; i < 3; ++i)
                        {
                            int32_t c = t2 * 3 + i;
                            int32_t t3 = mCornerToAdjacentFacet[c];
                            if (t3 != NO_FACET && mConnectedComponent[t3] != cnxId)
                            {
                                mConnectedComponent[t3] = cnxId;
                                S.push(t3);
                            }
                        }
                    }
                    
                    mComponentSize.push_back(nb);
                }
            }
        }
        
        // ===== INCREMENTAL TRIANGLE INSERTION =====
        
        void AddTriangles(std::vector<std::array<int32_t, 3>> const& candidateTriangles)
        {
            int32_t nbTriangles = static_cast<int32_t>(candidateTriangles.size());
            std::vector<bool> tIsClassified(nbTriangles, false);
            bool changed = true;
            int32_t maxIter = mParams.strictMode ? 5000 : mParams.maxIterations;
            int32_t iter = 0;
            
            while (changed && iter < maxIter)
            {
                changed = false;
                ++iter;
                
                for (int32_t t = 0; t < nbTriangles; ++t)
                {
                    if (!tIsClassified[t])
                    {
                        auto const& tri = candidateTriangles[t];
                        int32_t newT = AddTriangle(tri[0], tri[1], tri[2]);
                        bool classified = false;
                        
                        if (ConnectAndValidateTriangle(newT, classified))
                        {
                            changed = true;
                        }
                        else
                        {
                            RollbackTriangle();
                        }
                        
                        if (classified)
                        {
                            tIsClassified[t] = true;
                        }
                    }
                }
            }
        }
        
        int32_t AddTriangle(int32_t v0, int32_t v1, int32_t v2)
        {
            int32_t newIndex = static_cast<int32_t>(mTriangles.size());
            mTriangles.push_back({v0, v1, v2});
            
            // Expand arrays
            mCornerToAdjacentFacet.resize((newIndex + 1) * 3, NO_FACET);
            mNextCornerAroundVertex.resize((newIndex + 1) * 3, NO_CORNER);
            
            // Ensure vertex arrays are large enough
            int32_t maxV = std::max({v0, v1, v2});
            if (maxV >= static_cast<int32_t>(mVertexToCorner.size()))
            {
                mVertexToCorner.resize(maxV + 1, NO_CORNER);
            }
            
            Insert(newIndex);
            return newIndex;
        }
        
        void RollbackTriangle()
        {
            if (mTriangles.empty()) return;
            
            int32_t t = static_cast<int32_t>(mTriangles.size()) - 1;
            Remove(t, true);
            
            mTriangles.pop_back();
            mCornerToAdjacentFacet.resize(mTriangles.size() * 3);
            mNextCornerAroundVertex.resize(mTriangles.size() * 3);
        }
        
        // ===== VALIDATION TESTS =====
        
        bool ConnectAndValidateTriangle(int32_t t, bool& classified)
        {
            int32_t adjC[3];
            classified = false;
            
            // Test I: Check if edges are manifold
            if (!GetAdjacentCorners(t, adjC))
            {
                classified = true;
                return false;
            }
            
            // Test II: Geometric test - check normal agreement
            for (int i = 0; i < 3; ++i)
            {
                if (adjC[i] != NO_CORNER)
                {
                    int32_t t2 = CornerToFacet(adjC[i]);
                    if (!TrianglesNormalsAgree(t, t2))
                    {
                        classified = true;
                        return false;
                    }
                }
            }
            
            // Count neighbors
            int nbNeighbors = 0;
            for (int i = 0; i < 3; ++i)
            {
                if (adjC[i] != NO_CORNER) ++nbNeighbors;
            }
            
            // Test III: Combinatorial test on neighbor count
            if (nbNeighbors == 0)
            {
                return false;  // Isolated triangle, reject
            }
            
            if (nbNeighbors == 1 && !mParams.strictMode)
            {
                return false;  // Single neighbor, reject in non-strict mode
            }
            
            if (nbNeighbors == 1 && mParams.strictMode)
            {
                // Check if opposite vertex is isolated
                int32_t otherVertex = NO_FACET;
                for (int i = 0; i < 3; ++i)
                {
                    if (adjC[i] != NO_CORNER)
                    {
                        otherVertex = mTriangles[t][(i + 2) % 3];
                    }
                }
                
                if (otherVertex != NO_FACET)
                {
                    int32_t nbIncident = NbIncidentTriangles(otherVertex);
                    if (nbIncident > 1)
                    {
                        return false;
                    }
                }
            }
            
            // Connect the triangle
            ConnectAdjacentCorners(t, adjC);
            
            // Test IV: Check for non-manifold vertices
            for (int i = 0; i < 3; ++i)
            {
                int32_t v = mTriangles[t][i];
                bool moebius = false;
                if (VertexIsNonManifoldByExcess(v, moebius))
                {
                    classified = true;
                    return false;
                }
                if (moebius)
                {
                    classified = true;
                    return false;
                }
            }
            
            // Test V: Orientability
            if (!EnforceOrientationFromTriangle(t))
            {
                return false;
            }
            
            classified = true;
            return true;
        }
        
        // Returns true when the two adjacent triangles traverse their shared
        // edge in the same direction.  In a correctly-oriented manifold mesh,
        // neighbours traverse the shared edge in *opposite* directions.  When
        // they traverse it in the same direction the triangles point away from
        // each other, so the dot-product sign must be negated before testing.
        bool SameCombinorialOrientation(int32_t t1, int32_t t2) const
        {
            auto const& tri1 = mTriangles[t1];
            auto const& tri2 = mTriangles[t2];

            for (int i = 0; i < 3; ++i)
            {
                int32_t a = tri1[i];
                int32_t b = tri1[(i + 1) % 3];

                for (int j = 0; j < 3; ++j)
                {
                    if (tri2[j] == a && tri2[(j + 1) % 3] == b)
                    {
                        return true;   // same direction
                    }
                    if (tri2[j] == b && tri2[(j + 1) % 3] == a)
                    {
                        return false;  // opposite direction (manifold-correct)
                    }
                }
            }
            return false;
        }

        bool TrianglesNormalsAgree(int32_t t1, int32_t t2) const
        {
            Vector3<Real> n1 = ComputeTriangleNormal(t1);
            Vector3<Real> n2 = ComputeTriangleNormal(t2);

            Real d = Dot(n1, n2);
            // Match Geogram: negate d when both triangles traverse their
            // shared edge in the same direction (they face away from each
            // other), then apply the permissive threshold of -0.8 (~143°).
            if (SameCombinorialOrientation(t1, t2))
            {
                d = -d;
            }
            return d > static_cast<Real>(-0.8);
        }
        
        Vector3<Real> ComputeTriangleNormal(int32_t t) const
        {
            auto const& tri = mTriangles[t];
            Vector3<Real> const& p0 = mPoints[tri[0]];
            Vector3<Real> const& p1 = mPoints[tri[1]];
            Vector3<Real> const& p2 = mPoints[tri[2]];
            
            Vector3<Real> edge1 = p1 - p0;
            Vector3<Real> edge2 = p2 - p0;
            Vector3<Real> normal = Cross(edge1, edge2);
            Normalize(normal);
            return normal;
        }
        
        bool VertexIsNonManifoldByExcess(int32_t v, bool& moebius) const
        {
            moebius = false;
            int32_t c = mVertexToCorner[v];
            if (c == NO_CORNER) return false;
            
            int32_t c0 = c;
            int32_t nbTriangles = 0;
            bool hasLoop = false;
            
            // Traverse the circular list of corners around v
            do
            {
                ++nbTriangles;
                int32_t t = CornerToFacet(c);
                int32_t cNext = NextCornerAroundFacet(t, c);
                int32_t vNext = CornerToVertex(cNext);
                
                // Check if this edge has an adjacent triangle
                int32_t cAdj = FindAdjacentCorner(c, vNext);
                
                if (cAdj == NO_CORNER)
                {
                    // Boundary edge found
                    break;
                }
                
                c = cAdj;
                if (c == c0)
                {
                    hasLoop = true;
                    break;
                }
            } while (c != c0 && nbTriangles < 1000);
            
            // If we have a closed loop but there are more triangles,
            // we have a non-manifold by-excess configuration
            if (hasLoop)
            {
                int32_t totalTriangles = NbIncidentTriangles(v);
                if (totalTriangles > nbTriangles)
                {
                    return true;  // Non-manifold by excess
                }
            }
            
            return false;
        }
        
        // ===== ORIENTATION ENFORCEMENT =====
        
        bool EnforceOrientationFromTriangle(int32_t t)
        {
            int32_t adj[3];
            int32_t adjCnx[3];
            int32_t adjOri[3];
            
            // Get adjacent triangles and their properties
            for (int i = 0; i < 3; ++i)
            {
                int32_t c = t * 3 + i;
                adj[i] = mCornerToAdjacentFacet[c];
            }
            
            for (int i = 0; i < 3; ++i)
            {
                if (adj[i] == NO_FACET)
                {
                    adjOri[i] = 0;
                    adjCnx[i] = NO_CNX;
                }
                else
                {
                    adjOri[i] = TrianglesHaveSameOrientation(t, adj[i]) ? 1 : -1;
                    adjCnx[i] = GetConnectedComponent(adj[i]);
                }
            }
            
            // Check for Moebius strip: same component with opposite orientations
            for (int i = 0; i < 3; ++i)
            {
                if (adj[i] != NO_FACET)
                {
                    for (int j = i + 1; j < 3; ++j)
                    {
                        if (adjCnx[j] == adjCnx[i] && adjOri[j] != adjOri[i])
                        {
                            return false;  // Moebius strip detected
                        }
                    }
                }
            }
            
            // Find largest incident component
            int32_t largestNeighComp = NO_CNX;
            for (int i = 0; i < 3; ++i)
            {
                if (adjCnx[i] != NO_CNX)
                {
                    if (largestNeighComp == NO_CNX ||
                        mComponentSize[adjCnx[i]] > mComponentSize[adjCnx[largestNeighComp]])
                    {
                        largestNeighComp = i;
                    }
                }
            }
            
            if (largestNeighComp == NO_CNX)
            {
                // No adjacent components, create new one
                int32_t newCnx = static_cast<int32_t>(mComponentSize.size());
                EnsureComponentArraySize(t);
                mConnectedComponent[t] = newCnx;
                mComponentSize.push_back(1);
                return true;
            }
            
            // Orient t like the largest component
            int32_t comp = adjCnx[largestNeighComp];
            EnsureComponentArraySize(t);
            mConnectedComponent[t] = comp;
            ++mComponentSize[comp];
            
            if (adjOri[largestNeighComp] == -1)
            {
                FlipTriangle(t);
                for (int i = 0; i < 3; ++i)
                {
                    adjOri[i] = -adjOri[i];
                }
            }
            
            // Merge other incident components
            for (int i = 0; i < 3; ++i)
            {
                if (i != largestNeighComp && adj[i] != NO_FACET &&
                    GetConnectedComponent(adj[i]) != comp)
                {
                    MergeConnectedComponent(
                        GetConnectedComponent(adj[i]),
                        comp,
                        adjOri[i]
                    );
                }
            }
            
            return true;
        }
        
        bool TrianglesHaveSameOrientation(int32_t t1, int32_t t2) const
        {
            // Find common edge
            for (int i1 = 0; i1 < 3; ++i1)
            {
                int32_t v1 = mTriangles[t1][i1];
                int32_t v2 = mTriangles[t1][(i1 + 1) % 3];
                
                for (int i2 = 0; i2 < 3; ++i2)
                {
                    int32_t w1 = mTriangles[t2][i2];
                    int32_t w2 = mTriangles[t2][(i2 + 1) % 3];
                    
                    if (v1 == w2 && v2 == w1)
                    {
                        return true;  // Opposite edge direction = same orientation
                    }
                    if (v1 == w1 && v2 == w2)
                    {
                        return false;  // Same edge direction = opposite orientation
                    }
                }
            }
            
            return true;  // No common edge
        }
        
        void MergeConnectedComponent(int32_t fromComp, int32_t toComp, int32_t ori)
        {
            if (fromComp == toComp) return;

            // Find a seed triangle in fromComp and build the membership set.
            // O(T) scan runs once to seed the BFS; subsequent processing is
            // via adjacency links (matching Geogram's BFS-order approach) so
            // that Remove(t,false)+Insert(t) calls in FlipTriangle do not
            // corrupt the circular corner linked-lists.
            int32_t seed = NO_FACET;
            std::vector<bool> inFromComp(mTriangles.size(), false);
            for (int32_t t = 0; t < static_cast<int32_t>(mTriangles.size()); ++t)
            {
                if (GetConnectedComponent(t) == fromComp)
                {
                    inFromComp[t] = true;
                    if (seed == NO_FACET) seed = t;
                }
            }
            if (seed == NO_FACET) return;

            // BFS traversal via mCornerToAdjacentFacet.
            std::vector<bool> visited(mTriangles.size(), false);
            std::vector<int32_t> queue;
            queue.reserve(static_cast<size_t>(mComponentSize[fromComp]));
            queue.push_back(seed);
            visited[seed] = true;
            size_t head = 0;

            while (head < queue.size())
            {
                int32_t t = queue[head++];

                // Snapshot adjacencies before FlipTriangle rearranges corners.
                int32_t adj[3] = {
                    mCornerToAdjacentFacet[t * 3 + 0],
                    mCornerToAdjacentFacet[t * 3 + 1],
                    mCornerToAdjacentFacet[t * 3 + 2]
                };

                mConnectedComponent[t] = toComp;
                if (ori == -1)
                {
                    FlipTriangle(t);
                }

                for (int i = 0; i < 3; ++i)
                {
                    if (adj[i] != NO_FACET && !visited[adj[i]] && inFromComp[adj[i]])
                    {
                        visited[adj[i]] = true;
                        queue.push_back(adj[i]);
                    }
                }
            }

            mComponentSize[toComp] += mComponentSize[fromComp];
            mComponentSize[fromComp] = 0;
        }
        
        // ===== TOPOLOGY OPERATIONS =====
        
        void Insert(int32_t t)
        {
            for (int i = 0; i < 3; ++i)
            {
                int32_t c = t * 3 + i;
                int32_t v = mTriangles[t][i];
                
                if (mVertexToCorner[v] == NO_CORNER)
                {
                    mVertexToCorner[v] = c;
                    mNextCornerAroundVertex[c] = c;
                }
                else
                {
                    mNextCornerAroundVertex[c] = mNextCornerAroundVertex[mVertexToCorner[v]];
                    mNextCornerAroundVertex[mVertexToCorner[v]] = c;
                }
            }
        }
        
        void Remove(int32_t t, bool disconnect)
        {
            if (disconnect)
            {
                // Disconnect adjacent triangles
                for (int i = 0; i < 3; ++i)
                {
                    int32_t c = t * 3 + i;
                    int32_t t2 = mCornerToAdjacentFacet[c];
                    if (t2 != NO_FACET)
                    {
                        for (int j = 0; j < 3; ++j)
                        {
                            int32_t c2 = t2 * 3 + j;
                            if (mCornerToAdjacentFacet[c2] == t)
                            {
                                mCornerToAdjacentFacet[c2] = NO_FACET;
                            }
                        }
                    }
                }
            }
            
            // Remove from corner circular lists
            for (int i = 0; i < 3; ++i)
            {
                int32_t c = t * 3 + i;
                int32_t v = mTriangles[t][i];
                
                if (mNextCornerAroundVertex[c] == c)
                {
                    mVertexToCorner[v] = NO_CORNER;
                }
                else
                {
                    int32_t cPred = mNextCornerAroundVertex[c];
                    while (mNextCornerAroundVertex[cPred] != c)
                    {
                        cPred = mNextCornerAroundVertex[cPred];
                    }
                    mNextCornerAroundVertex[cPred] = mNextCornerAroundVertex[c];
                    mVertexToCorner[v] = cPred;
                }
            }
        }
        
        void FlipTriangle(int32_t t)
        {
            // Swap vertices 1 and 2
            std::swap(mTriangles[t][1], mTriangles[t][2]);
            
            // Update adjacency
            std::swap(mCornerToAdjacentFacet[t * 3 + 1], mCornerToAdjacentFacet[t * 3 + 2]);
            
            // Re-insert into topology
            Remove(t, false);
            Insert(t);
        }
        
        bool Connect(int32_t t)
        {
            int32_t adjC[3] = {NO_CORNER, NO_CORNER, NO_CORNER};
            if (!GetAdjacentCorners(t, adjC))
            {
                return false;
            }
            ConnectAdjacentCorners(t, adjC);
            return true;
        }
        
        bool GetAdjacentCorners(int32_t t, int32_t* adjC)
        {
            for (int i = 0; i < 3; ++i)
            {
                int32_t c1 = t * 3 + i;
                int32_t v2 = mTriangles[t][(i + 1) % 3];
                
                adjC[i] = NO_CORNER;
                
                // Traverse circular list around v1
                int32_t c2 = mNextCornerAroundVertex[c1];
                while (c2 != c1)
                {
                    int32_t t2 = CornerToFacet(c2);
                    
                    // Check both orientations
                    int32_t c3 = PrevCornerAroundFacet(t2, c2);
                    int32_t v3 = CornerToVertex(c3);
                    
                    if (v3 == v2)
                    {
                        if (adjC[i] == NO_CORNER)
                        {
                            adjC[i] = c3;
                        }
                        else
                        {
                            return false;  // Non-manifold edge
                        }
                    }
                    
                    c3 = NextCornerAroundFacet(t2, c2);
                    v3 = CornerToVertex(c3);
                    
                    if (v3 == v2)
                    {
                        if (adjC[i] == NO_CORNER)
                        {
                            adjC[i] = c2;
                        }
                        else
                        {
                            return false;  // Non-manifold edge
                        }
                    }
                    
                    c2 = mNextCornerAroundVertex[c2];
                }
            }
            
            return true;
        }
        
        void ConnectAdjacentCorners(int32_t t, int32_t* adjC)
        {
            for (int i = 0; i < 3; ++i)
            {
                if (adjC[i] != NO_CORNER)
                {
                    int32_t c = t * 3 + i;
                    mCornerToAdjacentFacet[c] = CornerToFacet(adjC[i]);
                    mCornerToAdjacentFacet[adjC[i]] = t;
                }
            }
        }
        
        int32_t FindAdjacentCorner(int32_t c, int32_t vNext) const
        {
            int32_t t = CornerToFacet(c);
            int32_t cStart = c;
            
            do
            {
                int32_t t2 = mCornerToAdjacentFacet[c];
                if (t2 == NO_FACET) return NO_CORNER;
                
                // Find corner in t2 adjacent to c
                for (int i = 0; i < 3; ++i)
                {
                    int32_t c2 = t2 * 3 + i;
                    if (mCornerToAdjacentFacet[c2] == t)
                    {
                        int32_t cNext = NextCornerAroundFacet(t2, c2);
                        int32_t v = CornerToVertex(cNext);
                        if (v == vNext)
                        {
                            return PrevCornerAroundFacet(t2, c2);
                        }
                    }
                }
                
                return NO_CORNER;
            } while (c != cStart);
            
            return NO_CORNER;
        }
        
        int32_t NbIncidentTriangles(int32_t v) const
        {
            int32_t result = 0;
            int32_t c = mVertexToCorner[v];
            if (c == NO_CORNER) return 0;
            
            int32_t c0 = c;
            do
            {
                ++result;
                c = mNextCornerAroundVertex[c];
            } while (c != c0 && result < 1000);
            
            return result;
        }
        
        // ===== MESH REORIENTATION & CLEANUP =====
        
        void ReorientMesh(std::vector<bool>& removeTriangle)
        {
            // Full geogram mesh_reorient implementation with priority-based propagation
            // Based on geogram's repair_reorient_facets_anti_moebius
            
            // Maximum distance from border for propagation heuristic
            // Value of 5 matches geogram's max_iter to balance between:
            // - Sufficient depth to reach interior facets
            // - Avoiding excessive computation on large meshes
            static constexpr int MAX_BORDER_DISTANCE = 5;
            
            // Compute distance from each facet to mesh border
            std::vector<uint8_t> borderDistance;
            ComputeBorderDistance(borderDistance, MAX_BORDER_DISTANCE);
            
            // Process facets in order of decreasing distance from border
            // This heuristic minimizes Moebius strip artifacts
            std::vector<bool> visited(mTriangles.size(), false);
            int32_t moebius_count = 0;
            size_t nb_visited = 0;
            
            for (int dist = MAX_BORDER_DISTANCE; dist >= 0; --dist)
            {
                for (size_t f = 0; f < mTriangles.size(); ++f)
                {
                    if (!visited[f] && !removeTriangle[f] && borderDistance[f] == dist)
                    {
                        std::stack<int32_t> Q;
                        Q.push(static_cast<int32_t>(f));
                        visited[f] = true;
                        nb_visited++;
                        
                        while (!Q.empty())
                        {
                            int32_t f1 = Q.top();
                            Q.pop();
                            
                            // Process all adjacent facets
                            for (int i = 0; i < 3; ++i)
                            {
                                int32_t c = f1 * 3 + i;
                                int32_t f2 = mCornerToAdjacentFacet[c];
                                
                                if (f2 != NO_FACET && !visited[f2] && !removeTriangle[f2])
                                {
                                    visited[f2] = true;
                                    nb_visited++;
                                    
                                    // Propagate orientation from already-visited neighbors
                                    PropagateOrientation(f2, visited, removeTriangle, moebius_count);
                                    
                                    Q.push(f2);
                                }
                            }
                        }
                    }
                    
                    if (nb_visited == mTriangles.size())
                    {
                        break;
                    }
                }
            }
        }
        
    private:
        // ===== PRIVATE HELPER METHODS FOR MESH REORIENTATION =====
        
        // Compute graph distance from each facet to mesh border
        void ComputeBorderDistance(std::vector<uint8_t>& distance, int maxIter)
        {
            // Validate maxIter constraint for uint8_t storage
            LogAssert(maxIter >= 0 && maxIter <= 255, 
                      "maxIter must be in range [0, 255] for uint8_t storage");
            
            distance.assign(mTriangles.size(), static_cast<uint8_t>(maxIter));
            
            // Mark border facets with distance 0
            for (size_t f = 0; f < mTriangles.size(); ++f)
            {
                if (FacetIsOnBorder(static_cast<int32_t>(f)))
                {
                    distance[f] = 0;
                }
            }
            
            // Propagate distances via breadth-first traversal
            // Complexity: O(maxIter * triangles)
            // Could be optimized with queue-based BFS, but maxIter is small (5)
            for (int iter = 1; iter < maxIter; ++iter)
            {
                for (size_t f = 0; f < mTriangles.size(); ++f)
                {
                    if (distance[f] == maxIter)
                    {
                        // Check if any neighbor has distance iter-1
                        for (int i = 0; i < 3; ++i)
                        {
                            int32_t c = static_cast<int32_t>(f) * 3 + i;
                            int32_t g = mCornerToAdjacentFacet[c];
                            
                            if (g != NO_FACET && distance[g] == (iter - 1))
                            {
                                distance[f] = static_cast<uint8_t>(iter);
                                break;
                            }
                        }
                    }
                }
            }
        }
        
        // Check if facet is on mesh border (has at least one unconnected edge)
        bool FacetIsOnBorder(int32_t f) const
        {
            for (int i = 0; i < 3; ++i)
            {
                int32_t c = f * 3 + i;
                if (mCornerToAdjacentFacet[c] == NO_FACET)
                {
                    return true;
                }
            }
            return false;
        }
        
        // Propagate orientation from already-visited neighbors
        // Detects and resolves Moebius configurations
        void PropagateOrientation(
            int32_t f,
            std::vector<bool> const& visited,
            std::vector<bool>& removeTriangle,
            int32_t& moebius_count)
        {
            // Count neighbors with compatible (+1) vs incompatible (-1) orientations
            int nb_plus = 0;
            int nb_minus = 0;
            
            for (int i = 0; i < 3; ++i)
            {
                int32_t c = f * 3 + i;
                int32_t f2 = mCornerToAdjacentFacet[c];
                
                if (f2 != NO_FACET && visited[f2])
                {
                    int ori = RelativeOrientation(f, c, f2);
                    if (ori == 1)
                    {
                        nb_plus++;
                    }
                    else if (ori == -1)
                    {
                        nb_minus++;
                    }
                }
            }
            
            // Moebius configuration: both orientations present
            if (nb_plus > 0 && nb_minus > 0)
            {
                moebius_count++;
                
                // Remove connections to minority orientation neighbors
                if (nb_plus > nb_minus)
                {
                    // Disconnect from negatively-oriented neighbors
                    for (int i = 0; i < 3; ++i)
                    {
                        int32_t c = f * 3 + i;
                        int32_t f2 = mCornerToAdjacentFacet[c];
                        
                        if (f2 != NO_FACET && visited[f2] && RelativeOrientation(f, c, f2) < 0)
                        {
                            Dissociate(f, f2);
                        }
                    }
                    nb_minus = 0;
                }
                else
                {
                    // Disconnect from positively-oriented neighbors
                    for (int i = 0; i < 3; ++i)
                    {
                        int32_t c = f * 3 + i;
                        int32_t f2 = mCornerToAdjacentFacet[c];
                        
                        if (f2 != NO_FACET && visited[f2] && RelativeOrientation(f, c, f2) > 0)
                        {
                            Dissociate(f, f2);
                        }
                    }
                    nb_plus = 0;
                }
            }
            
            // If majority orientation is negative, flip this facet
            if (nb_minus > 0)
            {
                FlipTriangle(f);
            }
        }
        
        // Compute relative orientation between two adjacent facets
        // Returns: +1 if compatible, -1 if incompatible, 0 if not adjacent
        int RelativeOrientation(int32_t f1, int32_t c11, int32_t f2) const
        {
            // Get edge vertices from f1
            int32_t c12 = NextCornerAroundFacet(f1, c11);
            int32_t v11 = CornerToVertex(c11);
            int32_t v12 = CornerToVertex(c12);
            
            // Check all edges of f2
            for (int i = 0; i < 3; ++i)
            {
                int32_t c21 = f2 * 3 + i;
                int32_t c22 = NextCornerAroundFacet(f2, c21);
                int32_t v21 = CornerToVertex(c21);
                int32_t v22 = CornerToVertex(c22);
                
                // Same edge direction = incompatible (both CCW or both CW)
                if (v11 == v21 && v12 == v22)
                {
                    return -1;
                }
                // Opposite edge direction = compatible (one CCW, one CW)
                if (v11 == v22 && v12 == v21)
                {
                    return 1;
                }
            }
            return 0;
        }
        
        // Remove adjacency between two facets
        void Dissociate(int32_t f1, int32_t f2)
        {
            // Remove f1 -> f2 adjacency
            for (int i = 0; i < 3; ++i)
            {
                int32_t c = f1 * 3 + i;
                if (mCornerToAdjacentFacet[c] == f2)
                {
                    mCornerToAdjacentFacet[c] = NO_FACET;
                }
            }
            
            // Remove f2 -> f1 adjacency
            for (int i = 0; i < 3; ++i)
            {
                int32_t c = f2 * 3 + i;
                if (mCornerToAdjacentFacet[c] == f1)
                {
                    mCornerToAdjacentFacet[c] = NO_FACET;
                }
            }
        }
        
    public:
        
        bool HasMarkedTriangles(std::vector<bool> const& removeTriangle) const
        {
            for (bool b : removeTriangle)
            {
                if (b) return true;
            }
            return false;
        }
        
        void DeleteMarkedTriangles(std::vector<bool> const& removeTriangle)
        {
            std::vector<std::array<int32_t, 3>> newTriangles;
            newTriangles.reserve(mTriangles.size());
            
            for (size_t i = 0; i < mTriangles.size(); ++i)
            {
                if (!removeTriangle[i])
                {
                    newTriangles.push_back(mTriangles[i]);
                }
            }
            
            mTriangles = std::move(newTriangles);
        }
        
        // ===== UTILITY FUNCTIONS =====
        
        inline int32_t CornerToFacet(int32_t c) const
        {
            return c / 3;
        }
        
        inline int32_t CornerToVertex(int32_t c) const
        {
            int32_t t = CornerToFacet(c);
            int32_t i = c % 3;
            return mTriangles[t][i];
        }
        
        inline int32_t NextCornerAroundFacet(int32_t t, int32_t c) const
        {
            int32_t i = c % 3;
            return t * 3 + ((i + 1) % 3);
        }
        
        inline int32_t PrevCornerAroundFacet(int32_t t, int32_t c) const
        {
            int32_t i = c % 3;
            return t * 3 + ((i + 2) % 3);
        }
        
        inline int32_t GetConnectedComponent(int32_t t) const
        {
            if (t < 0 || t >= static_cast<int32_t>(mConnectedComponent.size()))
            {
                return NO_CNX;
            }
            return mConnectedComponent[t];
        }
        
        void EnsureComponentArraySize(int32_t t)
        {
            if (t >= static_cast<int32_t>(mConnectedComponent.size()))
            {
                mConnectedComponent.resize(t + 1, NO_CNX);
            }
        }
        
        void GetResult(std::vector<std::array<int32_t, 3>>& outTriangles)
        {
            outTriangles = mTriangles;
        }
    };
}

#endif  // GTE_MATHEMATICS_CO3NE_MANIFOLD_EXTRACTOR_H
