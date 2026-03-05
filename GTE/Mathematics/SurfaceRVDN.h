// David Eberly, Geometric Tools, Redmond WA 98052
// Copyright (c) 1998-2026
// Distributed under the Boost Software License, Version 1.0.
// https://www.boost.org/LICENSE_1_0.txt
//
// Restricted Voronoi Diagram on a surface mesh in N dimensions.
//
// This is a direct translation of Geogram's
//   GEOGen::RestrictedVoronoiDiagram<DIM>  (generic_RVD.h)
//   GEOGen::Polygon::clip_by_plane_fast<DIM> (generic_RVD_polygon.h)
//   GetConnectedComponentsPrimalTriangles (RVD.cpp)
// adapted to GTE types (Vector<N,Real>, int32_t, std::, single-threaded,
// surface-only, no exact predicates, no symbolic perturbation).
//
// Geogram license: BSD 3-Clause (license-compatible with Boost Software License)
// Original Geogram copyright: (c) 2000-2022 Inria
// https://github.com/BrunoLevy/geogram

#pragma once

#include <GTE/Mathematics/Vector3.h>
#include <GTE/Mathematics/DelaunayNN.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <deque>
#include <limits>
#include <map>
#include <queue>
#include <stack>
#include <unordered_map>
#include <vector>

#include <vector>

namespace gte
{
    // ========================================================================
    // RDTRepair — topology-repair helpers for ComputeMultiNerveRDT.
    //
    // Implements three operations called in Geogram's mesh_postprocess_RDT
    // after the initial peninsula removal:
    //   ConnectFacets            ← repair_connect_facets
    //   ReorientFacetsAntiMoebius← repair_reorient_facets_anti_moebius
    //   SplitNonManifoldVertices ← repair_split_non_manifold_vertices
    //
    // The implementations are self-contained (no MeshRepair dependency) so
    // that SurfaceRVDN.h can be included without pulling in the full repair
    // pipeline (which would cause header-guard conflicts in some TUs).
    // ========================================================================
    struct RDTRepair
    {
        // ── ConnectFacets ────────────────────────────────────────────────────
        // Build adj[f*3+e] for each half-edge:
        //   ≥ 0  adjacent manifold triangle
        //   −1   boundary (unmatched)
        //   −2   non-manifold sentinel (3+ triangles on same undirected edge)
        // Uses undirected edge keys, matching Geogram's repair_connect_facets
        // which joins both same-direction and opposite-direction pairs.
        static void ConnectFacets(
            std::vector<std::array<int32_t,3>> const& tris,
            std::vector<int32_t>& adj)
        {
            static constexpr int32_t NM = -2;
            size_t n = tris.size();
            adj.assign(n * 3, -1);

            using EKey = std::pair<int32_t,int32_t>;
            std::map<EKey, std::pair<int32_t,int32_t>> edgeMap;

            for (size_t f = 0; f < n; ++f)
            {
                for (int e = 0; e < 3; ++e)
                {
                    int32_t va = tris[f][e];
                    int32_t vb = tris[f][(e + 1) % 3];
                    EKey key = {std::min(va,vb), std::max(va,vb)};

                    auto [it, inserted] = edgeMap.emplace(key, std::make_pair(int32_t(-1), int32_t(-1)));
                    if (it->second.first == NM) { /* already NM */ }
                    else if (it->second.first  < 0) { it->second.first  = (int32_t)f; }
                    else if (it->second.second < 0) { it->second.second = (int32_t)f; }
                    else { it->second = {NM, NM}; }
                }
            }

            for (size_t f = 0; f < n; ++f)
            {
                for (int e = 0; e < 3; ++e)
                {
                    int32_t va = tris[f][e];
                    int32_t vb = tris[f][(e + 1) % 3];
                    EKey key = {std::min(va,vb), std::max(va,vb)};
                    auto it = edgeMap.find(key);
                    if (it == edgeMap.end()) { continue; }
                    int32_t f0 = it->second.first, f1 = it->second.second;
                    if (f0 == NM || f1 < 0) { continue; }
                    adj[f*3+e] = (f0 == (int32_t)f) ? f1 : f0;
                }
            }
        }

        // ── RelativeOrientation ──────────────────────────────────────────────
        // +1 → f1,f2 consistently oriented (opposite directed edges on shared edge)
        // −1 → inconsistently oriented (same directed edge)
        //  0 → not sharing any edge
        static int32_t RelativeOrientation(
            std::vector<std::array<int32_t,3>> const& tris,
            int32_t f1, int32_t f2)
        {
            for (int e1 = 0; e1 < 3; ++e1)
            {
                int32_t v11 = tris[f1][e1], v12 = tris[f1][(e1+1)%3];
                for (int e2 = 0; e2 < 3; ++e2)
                {
                    int32_t v21 = tris[f2][e2], v22 = tris[f2][(e2+1)%3];
                    if (v11 == v21 && v12 == v22) { return -1; }
                    if (v11 == v22 && v12 == v21) { return  1; }
                }
            }
            return 0;
        }

        // ── Dissociate ───────────────────────────────────────────────────────
        // Remove the adjacency link between f1 and f2 in adj.
        static void Dissociate(std::vector<int32_t>& adj, int32_t f1, int32_t f2)
        {
            for (int e = 0; e < 3; ++e)
            {
                if (adj[f1*3+e] == f2) { adj[f1*3+e] = -1; }
                if (adj[f2*3+e] == f1) { adj[f2*3+e] = -1; }
            }
        }

        // ── PropagateOrientation ─────────────────────────────────────────────
        // Orient f relative to its already-visited neighbours; handle Möbius
        // conflicts by dissociating the minority edges.
        static void PropagateOrientation(
            std::vector<std::array<int32_t,3>>& tris,
            std::vector<int32_t>& adj,
            int32_t f,
            std::vector<bool> const& visited)
        {
            int32_t nbPlus = 0, nbMinus = 0;
            for (int e = 0; e < 3; ++e)
            {
                int32_t g = adj[f*3+e];
                if (g < 0 || !visited[(size_t)g]) { continue; }
                int32_t ori = RelativeOrientation(tris, f, g);
                if (ori ==  1) { ++nbPlus;  }
                if (ori == -1) { ++nbMinus; }
            }
            if (nbPlus != 0 && nbMinus != 0)
            {
                if (nbPlus > nbMinus)
                {
                    nbMinus = 0;
                    for (int e = 0; e < 3; ++e)
                    {
                        int32_t g = adj[f*3+e];
                        if (g < 0 || !visited[(size_t)g]) { continue; }
                        if (RelativeOrientation(tris, f, g) < 0) { Dissociate(adj, f, g); }
                    }
                }
                else
                {
                    nbPlus = 0;
                    for (int e = 0; e < 3; ++e)
                    {
                        int32_t g = adj[f*3+e];
                        if (g < 0 || !visited[(size_t)g]) { continue; }
                        if (RelativeOrientation(tris, f, g) > 0) { Dissociate(adj, f, g); }
                    }
                }
            }
            (void)nbPlus;
            if (nbMinus != 0)
            {
                std::swap(tris[f][1], tris[f][2]);
                // Update local-edge-to-adjacency mapping after the vertex swap:
                // swap(v1,v2) maps old local edge 0 ↔ new local edge 2, edge 1 unchanged.
                std::swap(adj[f*3+0], adj[f*3+2]);
            }
        }

        // ── ReorientFacetsAntiMoebius ────────────────────────────────────────
        // BFS from border triangles outward, reorienting for consistent normals.
        // Template parameter V is a vertex type with operator[](size_t) for coords.
        template <typename V>
        static void ReorientFacetsAntiMoebius(
            std::vector<V>& /*verts*/,
            std::vector<std::array<int32_t,3>>& tris,
            std::vector<int32_t>& adj)
        {
            size_t n = tris.size();
            if (n == 0) { return; }

            static constexpr int32_t MAX_ITER = 5;
            std::vector<int8_t> dist(n, (int8_t)MAX_ITER);
            for (size_t f = 0; f < n; ++f)
                for (int e = 0; e < 3; ++e)
                    if (adj[f*3+e] < 0) { dist[f] = 0; break; }

            for (int32_t iter = 1; iter < MAX_ITER; ++iter)
                for (size_t f = 0; f < n; ++f)
                    if (dist[f] == (int8_t)MAX_ITER)
                        for (int e = 0; e < 3; ++e)
                        {
                            int32_t g = adj[f*3+e];
                            if (g >= 0 && dist[(size_t)g] == (int8_t)(iter-1))
                            { dist[f] = (int8_t)iter; break; }
                        }

            std::vector<bool> visited(n, false);

            // Priority queue (max-heap on border distance) matching Geogram's
            // SimplePriorityQueue.  By always processing the highest-distance
            // (most interior) facet first, all of a facet's inner neighbours
            // are already oriented before its boundary neighbours are handled.
            // This reduces the number of Möbius conflicts and edge dissociations,
            // producing a cleaner adjacency for SplitNonManifoldVertices.
            using PQEntry = std::pair<int8_t, int32_t>;  // {dist, facet}
            std::priority_queue<PQEntry> pq;

            for (int32_t d = MAX_ITER; d >= 0; --d)
            {
                for (size_t f = 0; f < n; ++f)
                {
                    if (!visited[f] && dist[f] == (int8_t)d)
                    {
                        visited[f] = true;
                        pq.push({dist[f], (int32_t)f});
                        while (!pq.empty())
                        {
                            int32_t f1 = pq.top().second;
                            pq.pop();
                            for (int e = 0; e < 3; ++e)
                            {
                                int32_t f2 = adj[f1*3+e];
                                if (f2 < 0 || visited[(size_t)f2]) { continue; }
                                visited[(size_t)f2] = true;
                                PropagateOrientation(tris, adj, f2, visited);
                                pq.push({dist[(size_t)f2], f2});
                            }
                        }
                    }
                }
            }
        }

        // ── SplitNonManifoldVertices ─────────────────────────────────────────
        // Walk the fan of triangles around each vertex using adj.  If the fan
        // has multiple disconnected components, introduce new vertex copies.
        // Template parameter V is the vertex type (copied by value).
        template <typename V>
        static void SplitNonManifoldVertices(
            std::vector<V>& vertices,
            std::vector<std::array<int32_t,3>>& tris,
            std::vector<int32_t> const& adj)
        {
            size_t nF = tris.size();
            size_t nV = vertices.size();
            std::vector<bool> cVisited(nF * 3, false);
            std::vector<bool> vUsed(nV, false);
            std::vector<V>    newVerts;

            for (size_t f = 0; f < nF; ++f)
            {
                for (int lv = 0; lv < 3; ++lv)
                {
                    size_t c = f*3 + (size_t)lv;
                    if (cVisited[c]) { continue; }

                    int32_t oldV = tris[f][lv];
                    int32_t newV = oldV;
                    if (vUsed[(size_t)oldV])
                    {
                        newV = (int32_t)(nV + newVerts.size());
                        newVerts.push_back(vertices[(size_t)oldV]);
                    }
                    else { vUsed[(size_t)oldV] = true; }

                    // Walk forward: traverse adj through edge starting at oldV.
                    // Matching Geogram's repair_split_non_manifold_vertices forward pass.
                    size_t  curF  = f;
                    int     curLv = lv;
                    bool    hitBoundary = false;
                    for (;;)
                    {
                        cVisited[curF*3 + (size_t)curLv] = true;
                        tris[curF][curLv] = newV;

                        int32_t nxtF = adj[curF*3 + (size_t)curLv];
                        if (nxtF < 0) { hitBoundary = true; break; }   // boundary
                        if ((size_t)nxtF == f) { break; }               // fan loop closed

                        int found = -1;
                        for (int k = 0; k < 3; ++k)
                            if (tris[(size_t)nxtF][k] == oldV) { found = k; break; }
                        if (found < 0) { hitBoundary = true; break; }  // lost v (shouldn't happen)
                        curF  = (size_t)nxtF;
                        curLv = found;
                    }

                    // Walk backward (only when forward hit a boundary, not a loop).
                    // Translation of Geogram's backward pass in repair_split_non_manifold_vertices.
                    if (hitBoundary)
                    {
                        curF  = f;
                        curLv = lv;
                        for (;;)
                        {
                            // prev corner in same facet = (curLv+2)%3, then traverse its edge
                            int32_t prevF = adj[curF*3 + (size_t)((curLv + 2) % 3)];
                            if (prevF < 0) { break; }
                            int found = -1;
                            for (int k = 0; k < 3; ++k)
                                if (tris[(size_t)prevF][k] == oldV) { found = k; break; }
                            if (found < 0) { break; }
                            curF  = (size_t)prevF;
                            curLv = found;
                            cVisited[curF*3 + (size_t)curLv] = true;
                            tris[curF][curLv] = newV;
                        }
                    }
                }
            }
            if (!newVerts.empty())
                vertices.insert(vertices.end(), newVerts.begin(), newVerts.end());
        }
    };  // struct RDTRepair


    //
    // A vertex in an RVC polygon.  Stores N-D position plus edge metadata used
    // to drive the surface walk.  Matches GEOGen::Vertex (simplified: no
    // exact symbolic perturbation, no weight, bisectors capped at 2).
    //
    // Edge metadata semantics (same as Geogram):
    //   adjSeed  >=0  →  the edge starting at this vertex is a Voronoi bisector
    //                    with the seed at adjSeed.
    //   adjFacet >=0  →  the edge starting at this vertex is a mesh boundary
    //                    shared with mesh facet adjFacet.
    //
    // Symbolic bisector tracking (for Voronoi-vertex detection):
    //   nbBisectors  = 0,1,2  active bisectors at this vertex.
    //   bisectors[]  = seed indices of the active bisectors.
    //   A vertex with nbBisectors==2 is a "Voronoi vertex" where three seed
    //   cells meet; this is used to emit RDT triangle candidates.
    // ========================================================================
    template <typename Real, size_t N>
    struct RVDVertex
    {
        std::array<Real, N> pos{};
        int32_t adjSeed       = -1;
        int32_t adjFacet      = -1;
        int32_t nbBisectors   =  0;
        int32_t bisectors[2]  = {-1, -1};
    };

    // ========================================================================
    // RVDPolygon<Real, N>
    //
    // An ordered list of RVDVertex entries representing the current clipped
    // RVC polygon.  Provides the Sutherland-Hodgman plane-clip operation.
    //
    // clip_by_plane() is a direct translation of
    //   GEOGen::Polygon::clip_by_plane_fast<DIM>()
    // from geogram/src/lib/geogram/voronoi/generic_RVD_polygon.h.
    // ========================================================================
    template <typename Real, size_t N>
    class RVDPolygon
    {
    public:
        using Vertex = RVDVertex<Real, N>;

        std::vector<Vertex> V;

        size_t nb_vertices() const { return V.size(); }
        bool   empty()       const { return V.empty(); }
        void   clear()             { V.clear(); }
        void   swap(RVDPolygon& o) { V.swap(o.V); }

        // -----------------------------------------------------------------
        // Clip this polygon against the bisector halfspace of (seed_i, seed_j).
        // Keeps vertices closer to seed_i in N-D Euclidean distance:
        //   2*(pos·(pi−pj)) ≥ |pi|²−|pj|²
        //
        // Direct translation of clip_by_plane_fast<DIM> from Geogram.
        // Entering intersection (going from outside to inside halfspace):
        //   I.adjFacet = prev.adjFacet  (copy_edge_from(*prev_vk))
        //   I.adjSeed  = j_idx
        // Leaving intersection (going from inside to outside halfspace):
        //   I.adjFacet = -1
        //   I.adjSeed  = cur.adjSeed
        // -----------------------------------------------------------------
        void clip_by_plane(
            RVDPolygon&              target,
            std::array<Real,N> const& pi,   // seed_i N-D position
            std::array<Real,N> const& pj,   // seed_j N-D position
            int32_t                  j_idx  // seed j index (for adjSeed)
        ) const
        {
            target.clear();
            const size_t nv = V.size();
            if (nv == 0)
            {
                return;
            }

            // d = (pi+pj)·(pi−pj) = |pi|² − |pj|²
            Real d = Real(0);
            for (size_t c = 0; c < N; ++c)
            {
                d += (pi[c] + pj[c]) * (pi[c] - pj[c]);
            }

            // Bootstrap with last vertex
            Real prevL = DotDiff(V[nv - 1].pos, pi, pj);
            int  prevS = Sgn2(prevL, d);

            for (size_t k = 0; k < nv; ++k)
            {
                Vertex const& vk   = V[k];
                Vertex const& prev = V[(k + nv - 1) % nv];
                Real l = DotDiff(vk.pos, pi, pj);
                int  s = Sgn2(l, d);

                // Edge crosses the bisector boundary
                if (s != prevS && prevS != 0)
                {
                    // Barycentric coordinate lambda1 (weight for prev vertex)
                    Real denom = Real(2) * (prevL - l);
                    Real lam1, lam2;
                    if (std::abs(denom) < Real(1e-20))
                    {
                        lam1 = lam2 = Real(0.5);
                    }
                    else
                    {
                        lam1 = (d - Real(2) * l) / denom;
                        lam2 = Real(1) - lam1;
                    }

                    Vertex I;
                    for (size_t c = 0; c < N; ++c)
                    {
                        I.pos[c] = lam1 * prev.pos[c] + lam2 * vk.pos[c];
                    }

                    // Symbolic: union of both endpoint bisectors + j
                    MergeBisectors(I, prev, vk, j_idx);

                    if (s > 0)
                    {
                        // Entering halfspace: copy edge metadata from prev
                        // (matches Geogram's copy_edge_from(*prev_vk))
                        I.adjFacet = prev.adjFacet;
                        I.adjSeed  = j_idx;
                    }
                    else
                    {
                        // Leaving halfspace: inherit outer edge's seed
                        // (matches Geogram's I.set_adjacent_seed(vk->adjacent_seed()))
                        I.adjFacet = -1;
                        I.adjSeed  = vk.adjSeed;
                    }
                    target.V.push_back(I);
                }

                if (s > 0)
                {
                    target.V.push_back(vk);
                }

                prevS = s;
                prevL = l;
            }
        }

    private:
        // pos · (pi−pj)  used by the halfspace predicate
        static Real DotDiff(
            std::array<Real,N> const& pos,
            std::array<Real,N> const& pi,
            std::array<Real,N> const& pj)
        {
            Real l = Real(0);
            for (size_t c = 0; c < N; ++c)
            {
                l += pos[c] * (pi[c] - pj[c]);
            }
            return l;
        }

        // sign of (2*l − d)
        static int Sgn2(Real l, Real d)
        {
            Real x = Real(2) * l - d;
            return (x > Real(0)) ? 1 : (x < Real(0)) ? -1 : 0;
        }

        // Build symbolic bisectors for intersection vertex I:
        // result = {j_idx} ∪ prev.bisectors ∪ cur.bisectors, capped at 2
        // (translation of SymbolicVertex::intersect_symbolic + add_bisector)
        static void MergeBisectors(
            Vertex& I, Vertex const& prev, Vertex const& cur, int32_t j)
        {
            I.nbBisectors   = 0;
            I.bisectors[0]  = I.bisectors[1] = -1;

            auto add = [&](int32_t b)
            {
                if (b < 0 || I.nbBisectors >= 2)
                {
                    return;
                }
                for (int k = 0; k < I.nbBisectors; ++k)
                {
                    if (I.bisectors[k] == b)
                    {
                        return;  // already present
                    }
                }
                I.bisectors[I.nbBisectors++] = b;
            };

            add(j);
            for (int k = 0; k < prev.nbBisectors; ++k) { add(prev.bisectors[k]); }
            for (int k = 0; k < cur.nbBisectors;  ++k) { add(cur.bisectors[k]);  }
        }
    };

    // ========================================================================
    // SurfaceRVDN<Real, N>
    //
    // Restricted Voronoi Diagram on a surface mesh embedded in N-D.
    //
    // Direct translation of GEOGen::RestrictedVoronoiDiagram<DIM>::
    //   compute_surfacic_with_cnx_priority() from generic_RVD.h.
    //
    // The surface mesh is given in N-D (e.g., 6-D lifted vertices for
    // anisotropic CVT).  Seeds are N-D points (Delaunay sites).
    //
    // ForEachPolygon() walks the surface, computing per-(seed,facet) RVC
    // polygons by Sutherland-Hodgman clipping, and tracking connected
    // components of each seed's Restricted Voronoi Cell.
    // ========================================================================
    template <typename Real, size_t N>
    class SurfaceRVDN
    {
    public:
        using PointN  = std::array<Real, N>;
        using Polygon = RVDPolygon<Real, N>;
        using Vertex  = RVDVertex<Real, N>;

        // -----------------------------------------------------------------
        // Initialize with surface mesh and seeds.
        // liftedVerts: N-D positions of mesh vertices.
        // tris:        triangle connectivity (integer indices into liftedVerts).
        // seeds:       N-D positions of Voronoi seeds.
        // delaunay:    built Delaunay/NN structure over seeds for neighbor queries.
        // -----------------------------------------------------------------
        void Initialize(
            std::vector<PointN> const&                     liftedVerts,
            std::vector<std::array<int32_t, 3>> const&     tris,
            std::vector<PointN> const&                     seeds,
            DelaunayNN<Real, N>&                           delaunay)
        {
            mLiftedVerts = &liftedVerts;
            mTris        = &tris;
            mSeeds       = &seeds;
            mDelaunay    = &delaunay;
            mNumFacets   = tris.size();

            // Per-facet edge adjacency table: mFacetAdj[f*3 + e] = adjacent facet
            // through edge (tri[e], tri[(e+1)%3]), or -1 if boundary.
            mFacetAdj.assign(mNumFacets * 3, int32_t(-1));

            // Build edge→triangle map.  Entry (-2,-2) flags a non-manifold
            // edge (3+ incident triangles); those are treated as boundaries.
            // Entry (f,-1) flags an unmatched (boundary) edge.
            // Matching Geogram's repair_connect_facets which marks non-manifold
            // edges as NON_MANIFOLD and does NOT set adjacency for them.
            using EKey = uint64_t;
            static constexpr int32_t NM = -2;  // non-manifold sentinel
            std::unordered_map<EKey, std::pair<int32_t, int32_t>> edgeMap;
            edgeMap.reserve(mNumFacets * 3);

            for (size_t f = 0; f < mNumFacets; ++f)
            {
                auto const& tri = (*mTris)[f];
                for (int e = 0; e < 3; ++e)
                {
                    EKey key = EdgeKey(tri[e], tri[(e + 1) % 3]);
                    auto [it, ok] = edgeMap.emplace(key, std::make_pair(-1, -1));
                    (void)ok;
                    if (it->second.first == NM)
                    {
                        // Already flagged non-manifold — stay flagged
                    }
                    else if (it->second.first < 0)
                    {
                        it->second.first  = static_cast<int32_t>(f);
                    }
                    else if (it->second.second < 0)
                    {
                        it->second.second = static_cast<int32_t>(f);
                    }
                    else
                    {
                        // Third triangle on this edge → non-manifold; treat as boundary
                        it->second = {NM, NM};
                    }
                }
            }

            for (size_t f = 0; f < mNumFacets; ++f)
            {
                auto const& tri = (*mTris)[f];
                for (int e = 0; e < 3; ++e)
                {
                    EKey key = EdgeKey(tri[e], tri[(e + 1) % 3]);
                    auto it  = edgeMap.find(key);
                    if (it != edgeMap.end())
                    {
                        int32_t f0 = it->second.first;
                        int32_t f1 = it->second.second;
                        // Skip non-manifold edges (f0==NM) and boundary edges (f1<0)
                        if (f0 == NM || f1 < 0) { continue; }
                        mFacetAdj[f * 3 + e] = (f0 == static_cast<int32_t>(f)) ? f1 : f0;
                    }
                }
            }
        }

        // -----------------------------------------------------------------
        // Walk the surface and call action for every non-empty (seed,facet)
        // RVC polygon.
        //
        // Signature of action:
        //   action(int32_t seed, int32_t facet,
        //          RVDPolygon<Real,N> const& poly,
        //          bool componentChanged,
        //          int32_t currentComponent)
        //
        // componentChanged is true when the walk has just started a new
        // connected component of seed's RVC (matching Geogram's
        //   RVD_.connected_component_changed()).
        //
        // Direct translation of compute_surfacic_with_cnx_priority().
        // -----------------------------------------------------------------
        template <typename Action>
        void ForEachPolygon(Action&& action)
        {
            // facetStamp[f] = last seed that stamped facet f (prevents
            // duplicate pushes to the adjacent_facets stack within one component).
            static constexpr int32_t NO_STAMP = int32_t(-1);
            std::vector<int32_t> facetStamp(mNumFacets, NO_STAMP);

            // visited: (facet,seed) pairs whose polygon touched the RVC border;
            // maps to the component ID.  Used for RDT triangle generation via
            // GetFacetSeedComponent().
            mFacetSeedComp.clear();
            mCurrentComp = 0;

            struct FacetSeed
            {
                int32_t f, s;
            };
            std::deque<FacetSeed> adjacentSeeds;
            std::stack<int32_t>   adjacentFacets;

            // Ping-pong polygon buffers for Sutherland-Hodgman reentrant clipping
            Polygon P_orig, P1, P2;
            int32_t P_orig_idx = int32_t(-1);

            // Outer loop: seed all unvisited facets (discovers disconnected components)
            for (size_t startF = 0; startF < mNumFacets; ++startF)
            {
                if (facetStamp[startF] != NO_STAMP)
                {
                    continue;
                }

                int32_t nearSeed = FindNearestSeed(static_cast<int32_t>(startF));
                adjacentSeeds.push_back({static_cast<int32_t>(startF), nearSeed});

                // Propagate along the Delaunay-graph (different seeds) and the
                // facet-graph (same seed, connected facets).
                while (!adjacentSeeds.empty())
                {
                    int32_t curF = adjacentSeeds.front().f;
                    int32_t curS = adjacentSeeds.front().s;
                    adjacentSeeds.pop_front();

                    // Already stamped for this seed in a previous component pass
                    if (facetStamp[curF] == curS)
                    {
                        continue;
                    }
                    // Already fully visited (marked with component info)
                    if (mFacetSeedComp.count(FacetSeedKey(curF, curS)))
                    {
                        continue;
                    }

                    // Start a new connected component
                    bool compChanged = true;
                    adjacentFacets.push(curF);
                    facetStamp[curF] = curS;

                    while (!adjacentFacets.empty())
                    {
                        int32_t f = adjacentFacets.top();
                        adjacentFacets.pop();

                        // Re-initialize from mesh facet only when facet changes
                        if (P_orig_idx != f)
                        {
                            InitPolygon(f, P_orig);
                            P_orig_idx = f;
                        }

                        // Clip facet f against curS's Voronoi cell
                        Polygon* poly = ClipCellFacet(curS, P_orig, P1, P2);

                        // If this (facet,seed) pair was already recorded from a previous
                        // connected component of the same seed, we must still propagate
                        // adjacency through f so the BFS covers the full Voronoi cell
                        // region on each mesh connected component (matching Geogram's
                        // behaviour of no inner-BFS early-exit).  We skip the polygon
                        // action and adjSeed re-pushes to avoid double-counting, but we
                        // DO traverse the mesh edges so adjacent facets are not missed.
                        if (mFacetSeedComp.count(FacetSeedKey(f, curS)))
                        {
                            // Propagate adjacency only (no action, no adjSeed pushes)
                            for (auto const& v : poly->V)
                            {
                                if (v.adjFacet >= 0
                                    && facetStamp[v.adjFacet] != curS)
                                {
                                    facetStamp[v.adjFacet] = curS;
                                    adjacentFacets.push(v.adjFacet);
                                }
                            }
                            // Update component mapping so triangles use the current
                            // component (same overwrite-last behaviour as Geogram)
                            mFacetSeedComp[FacetSeedKey(f, curS)] = mCurrentComp;
                            continue;
                        }

                        if (!poly->empty())
                        {
                            action(curS, f, *poly, compChanged, mCurrentComp);
                            compChanged = false;
                        }

                        // Propagate to adjacent facets and seeds
                        bool touchesBorder = false;
                        for (auto const& v : poly->V)
                        {
                            // Mesh edge: visit adjacent facet in same seed's cell
                            if (v.adjFacet >= 0
                                && facetStamp[v.adjFacet] != curS)
                            {
                                facetStamp[v.adjFacet] = curS;
                                adjacentFacets.push(v.adjFacet);
                            }
                            // Bisector edge: schedule adjacent seed's cell on this facet
                            if (v.adjSeed >= 0)
                            {
                                touchesBorder = true;
                                if (!mFacetSeedComp.count(FacetSeedKey(f, v.adjSeed)))
                                {
                                    adjacentSeeds.push_back({f, v.adjSeed});
                                }
                            }
                        }

                        // Record this (facet,seed) → component mapping
                        if (touchesBorder)
                        {
                            mFacetSeedComp[FacetSeedKey(f, curS)] = mCurrentComp;
                        }
                    } // inner facets loop

                    ++mCurrentComp;
                } // seed deque loop
            } // outer facet loop
        }

        // Returns the connected-component ID of seed s on facet f, or -1 if
        // not recorded (the polygon didn't touch any Voronoi boundary on f).
        int32_t GetFacetSeedComponent(int32_t f, int32_t s) const
        {
            auto it = mFacetSeedComp.find(FacetSeedKey(f, s));
            return (it != mFacetSeedComp.end()) ? it->second : int32_t(-1);
        }

        int32_t TotalComponents() const { return mCurrentComp; }

    private:
        // -----------------------------------------------------------------
        // Find the nearest seed to the centroid of mesh facet f in N-D
        // (translation of find_seed_near_facet).
        // -----------------------------------------------------------------
        int32_t FindNearestSeed(int32_t f) const
        {
            auto const& tri = (*mTris)[f];
            PointN c{};
            for (int i = 0; i < 3; ++i)
            {
                for (size_t d = 0; d < N; ++d)
                {
                    c[d] += (*mLiftedVerts)[tri[i]][d];
                }
            }
            for (size_t d = 0; d < N; ++d)
            {
                c[d] /= Real(3);
            }

            int32_t nearest = 0;
            Real    minD    = std::numeric_limits<Real>::max();
            for (size_t s = 0; s < mSeeds->size(); ++s)
            {
                Real dist = DistSq(c, (*mSeeds)[s]);
                if (dist < minD)
                {
                    minD    = dist;
                    nearest = static_cast<int32_t>(s);
                }
            }
            return nearest;
        }

        // -----------------------------------------------------------------
        // Initialize polygon from mesh triangle f.
        // Sets adjFacet for each vertex using the precomputed adjacency table.
        // Translation of Polygon::initialize_from_mesh_facet().
        // -----------------------------------------------------------------
        void InitPolygon(int32_t f, Polygon& poly) const
        {
            auto const& tri = (*mTris)[f];
            poly.clear();
            for (int lv = 0; lv < 3; ++lv)
            {
                Vertex v;
                auto const& p = (*mLiftedVerts)[tri[lv]];
                for (size_t c = 0; c < N; ++c)
                {
                    v.pos[c] = p[c];
                }
                v.adjSeed  = -1;
                // adjFacet = facet adjacent through edge (lv → lv+1)
                v.adjFacet = mFacetAdj[f * 3 + lv];
                poly.V.push_back(v);
            }
        }

        // -----------------------------------------------------------------
        // Clip facet F against seed's Voronoi cell using Delaunay neighbors.
        // Direct translation of Geogram's clip_by_cell_SR: uses a dynamic
        // neighborhood that is enlarged iteratively (matching Geogram's
        // enlarge_neighborhood loop) until the security-radius criterion is
        // satisfied, i.e. all remaining unprocessed neighbors are farther
        // than 2*R from the seed (where R = max polygon vertex distance).
        // Ping-pong clipping: returns pointer to the result polygon.
        // -----------------------------------------------------------------
        Polygon* ClipCellFacet(
            int32_t          seed,
            Polygon const&   F,
            Polygon&         P1,
            Polygon&         P2)
        {
            P1.V = F.V;          // copy facet into first buffer
            Polygon* ping = &P1;
            Polygon* pong = &P2;

            PointN const& pi = (*mSeeds)[seed];

            // Compute max N-D squared distance from seed to polygon vertices
            // (security radius R²)
            Real R2 = Real(0);
            for (auto const& v : ping->V)
            {
                R2 = std::max(R2, DistSq(pi, v.pos));
            }

            // Direct translation of Geogram's clip_by_cell_SR:
            // Iteratively enlarge the neighborhood until the security radius
            // criterion is met (dij > 4.1*R2 for the next unclipped neighbor).
            // jj tracks the position up to which we have already clipped;
            // only newly-added neighbors are sorted and clipped on each
            // enlargement pass, matching Geogram's incremental jj advance.
            size_t nbTotalSeeds = mDelaunay->GetNumVertices();

            // Fetch initial neighbor list and sort it by distance.
            // neighbors is grown in-place on each enlargement pass; only the
            // new suffix [prevSize..newSize) is sorted and merged each time.
            std::vector<int32_t> neighbors = mDelaunay->GetNeighbors(seed);
            std::sort(neighbors.begin(), neighbors.end(),
                [&](int32_t a, int32_t b) {
                    return DistSq(pi, (*mSeeds)[a]) < DistSq(pi, (*mSeeds)[b]);
                });

            size_t jj = 0;

            while (true)
            {
                bool srSatisfied = false;
                for (; jj < neighbors.size(); ++jj)
                {
                    int32_t j = neighbors[jj];
                    if (j == seed)
                    {
                        continue;
                    }
                    PointN const& pj = (*mSeeds)[j];
                    Real dij = DistSq(pi, pj);

                    // Security radius: if d(pi,pj) > 2*R the remaining
                    // neighbors (sorted) cannot affect the polygon.
                    if (dij > Real(4.1) * R2)
                    {
                        srSatisfied = true;
                        break;
                    }

                    pong->clear();
                    ping->clip_by_plane(*pong, pi, pj, j);
                    std::swap(ping, pong);

                    if (ping->empty())
                    {
                        return ping;
                    }

                    // Update R² after clipping (polygon shrinks)
                    R2 = Real(0);
                    for (auto const& v : ping->V)
                    {
                        R2 = std::max(R2, DistSq(pi, v.pos));
                    }
                }

                if (srSatisfied)
                {
                    break;  // security radius satisfied; done
                }

                // All current neighbors processed and SR not yet satisfied:
                // enlarge neighborhood (matching Geogram's enlarge_neighborhood
                // growth schedule: nb += nb/8 for nb>8, else nb++)
                size_t prevSize = neighbors.size();
                size_t nb = prevSize;
                if (nb > 8)
                {
                    nb += nb / 8;
                }
                else
                {
                    nb++;
                }
                nb = std::min(nb, nbTotalSeeds - 1);
                if (nb <= prevSize)
                {
                    break;  // can't enlarge further (already at max)
                }
                mDelaunay->EnlargeNeighborhood(seed, nb);

                // Fetch the enlarged list and sort only the new suffix
                // [prevSize..newSize), then merge it into the sorted prefix.
                std::vector<int32_t> newNeighbors = mDelaunay->GetNeighbors(seed);
                if (newNeighbors.size() <= prevSize)
                {
                    break;  // enlargement made no progress
                }
                // Sort the new suffix by distance and merge into neighbors.
                std::sort(newNeighbors.begin() + static_cast<ptrdiff_t>(prevSize),
                    newNeighbors.end(),
                    [&](int32_t a, int32_t b) {
                        return DistSq(pi, (*mSeeds)[a]) < DistSq(pi, (*mSeeds)[b]);
                    });
                neighbors = std::move(newNeighbors);
                // jj remains at prevSize: process starting from the new suffix
                // (the already-sorted prefix was fully processed in the prior pass)
            }
            return ping;
        }

        // -----------------------------------------------------------------
        // Helper utilities
        // -----------------------------------------------------------------
        static Real DistSq(PointN const& a, PointN const& b)
        {
            Real s = Real(0);
            for (size_t c = 0; c < N; ++c)
            {
                Real d = a[c] - b[c];
                s += d * d;
            }
            return s;
        }

        static uint64_t FacetSeedKey(int32_t f, int32_t s)
        {
            return (static_cast<uint64_t>(static_cast<uint32_t>(f)) << 32)
                 |  static_cast<uint64_t>(static_cast<uint32_t>(s));
        }

        static uint64_t EdgeKey(int32_t a, int32_t b)
        {
            if (a > b) { std::swap(a, b); }
            return (static_cast<uint64_t>(static_cast<uint32_t>(a)) << 32)
                 |  static_cast<uint64_t>(static_cast<uint32_t>(b));
        }

        // -----------------------------------------------------------------
        // Data members
        // -----------------------------------------------------------------
        std::vector<PointN> const*                 mLiftedVerts = nullptr;
        std::vector<std::array<int32_t,3>> const*  mTris        = nullptr;
        std::vector<PointN> const*                 mSeeds       = nullptr;
        DelaunayNN<Real, N>*                        mDelaunay    = nullptr;
        size_t                                      mNumFacets   = 0;

        std::vector<int32_t>                        mFacetAdj;

        // (facet, seed) → connected-component ID
        std::unordered_map<uint64_t, int32_t>       mFacetSeedComp;
        int32_t                                     mCurrentComp = 0;
    };


    // ========================================================================
    // ComputeMultiNerveRDT
    //
    // Top-level function that drives SurfaceRVDN and collects the
    // multi-nerve RDT output, matching Geogram's:
    //   GetConnectedComponentsPrimalTriangles  (RVD.cpp)
    //   + CVT::compute_surface(multinerve=true) (CVT.cpp)
    //
    // Per-component output vertices are the area-weighted N-D centroids
    // of their RVC polygons, projected to 3D by taking the first 3 coords
    // (correct because the N-D lifting preserves 3D positions in dims 0–2).
    //
    // RDT triangle candidates are emitted when a vertex with nbBisectors==2
    // is found (a Voronoi vertex where three seed cells meet), matching
    // GetConnectedComponentsPrimalTriangles::operator().
    // Duplicate triangles are deduplicated via canonical sort.
    // Winding is resolved from accumulated original mesh face normals.
    //
    // outVertices: one 3D point per connected component of any RVC.
    // outTriangles: new RDT connectivity over outVertices.
    // Returns false if no triangles were produced.
    // ========================================================================
    template <typename Real, size_t N>
    bool ComputeMultiNerveRDT(
        std::vector<std::array<Real, N>> const&      seeds,
        std::vector<std::array<Real, N>> const&      liftedVerts,
        std::vector<std::array<int32_t, 3>> const&   tris,
        DelaunayNN<Real, N>&                         delaunay,
        std::vector<Vector3<Real>>&                  outVertices,
        std::vector<std::array<int32_t, 3>>&         outTriangles)
    {
        SurfaceRVDN<Real, N> rvd;
        rvd.Initialize(liftedVerts, tris, seeds, delaunay);

        // Per-component centroid accumulators (in 3D = first 3 N-D coords).
        // Allocated on-demand as components are discovered.
        struct CompData
        {
            Real wx = Real(0), wy = Real(0), wz = Real(0);
            Real mass = Real(0);
        };
        std::vector<CompData> comps;

        // RDT triangle candidates: canonical-sorted key → accumulated 3D face normal
        struct NormAccum { Real nx = Real(0), ny = Real(0), nz = Real(0); };
        using TriKey = std::array<int32_t, 3>;
        std::map<TriKey, NormAccum> candidates;

        // State for the running connected component
        int32_t prevComp = -1;
        Real    curWx = Real(0), curWy = Real(0), curWz = Real(0);
        Real    curMass = Real(0);

        // N-D triangle area (Heron's formula on N-D edge lengths)
        // Translation of Geom::triangle_area(p0,p1,p2, DIM) from
        // geogram/src/lib/geogram/basic/geometry_nd.h.
        auto TriAreaND = [&](
            std::array<Real,N> const& p0,
            std::array<Real,N> const& p1,
            std::array<Real,N> const& p2) -> Real
        {
            Real a = Real(0), b = Real(0), c = Real(0);
            for (size_t d = 0; d < N; ++d)
            {
                Real e0 = p0[d] - p1[d];
                Real e1 = p1[d] - p2[d];
                Real e2 = p2[d] - p0[d];
                a += e0 * e0;
                b += e1 * e1;
                c += e2 * e2;
            }
            a = std::sqrt(a);
            b = std::sqrt(b);
            c = std::sqrt(c);
            Real s  = Real(0.5) * (a + b + c);
            Real A2 = s * (s - a) * (s - b) * (s - c);
            return std::sqrt(std::max(A2, Real(0)));
        };

        // Flush the current component into comps[]
        auto FinalizeComp = [&](int32_t comp)
        {
            if (comp < 0) { return; }
            while (static_cast<int32_t>(comps.size()) <= comp)
            {
                comps.push_back({});
            }
            Real scal = (curMass > Real(1e-30)) ? (Real(1) / curMass) : Real(0);
            comps[comp].wx   = curWx   * scal;
            comps[comp].wy   = curWy   * scal;
            comps[comp].wz   = curWz   * scal;
            comps[comp].mass = curMass;
        };

        // ForEachPolygon callback — direct translation of
        // GetConnectedComponentsPrimalTriangles::operator() from Geogram's RVD.cpp.
        //
        // Triangle candidates are emitted DURING THE WALK (not post-pass),
        // matching Geogram exactly: "they can be generated several times,
        // since we cannot know in advance whether the other instances of the
        // Voronoi vertex were finalized or not. Duplicate triangles are then
        // filtered-out by client code." (Geogram comment verbatim)
        //
        // When a 2-bisector vertex is found, we immediately attempt to look up
        // the components of the other two seeds on the current facet via
        // GetFacetSeedComponent. If either lookup returns -1 (the other seed
        // hasn't processed this facet yet, or has a degenerate RVC on it), the
        // triangle is skipped — it will be re-emitted when a later seed processes
        // the same (or adjacent) facet where all three components are available.
        // The candidates map deduplicates via canonical sort.

        rvd.ForEachPolygon([&](
            int32_t seed, int32_t facet,
            RVDPolygon<Real,N> const& P,
            bool compChanged, int32_t compID)
        {
            // -- connected_component_changed() handling --
            if (compChanged)
            {
                FinalizeComp(prevComp);
                prevComp = compID;
                curWx = curWy = curWz = curMass = Real(0);
            }

            // Accumulate N-D centroid (fan triangulation from vertex 0).
            // Translation of the mass/barycenter accumulation loop in
            // GetConnectedComponentsPrimalTriangles::operator().
            const size_t nv = P.nb_vertices();
            for (size_t i = 1; i + 1 < nv; ++i)
            {
                Real area = TriAreaND(P.V[0].pos, P.V[i].pos, P.V[i+1].pos);
                Real a3   = area / Real(3);
                // Only accumulate first 3 coords (3D position on surface)
                curWx   += a3 * (P.V[0].pos[0] + P.V[i].pos[0] + P.V[i+1].pos[0]);
                curWy   += a3 * (P.V[0].pos[1] + P.V[i].pos[1] + P.V[i+1].pos[1]);
                curWz   += a3 * (P.V[0].pos[2] + P.V[i].pos[2] + P.V[i+1].pos[2]);
                curMass += area;
            }

            // Detect Voronoi vertices (nbBisectors==2) and emit RDT triangle
            // candidates.  Matching Geogram's triangle-generation loop in
            // GetConnectedComponentsPrimalTriangles::operator():
            //   v1 = current_connected_component()
            //   v2 = get_facet_seed_connected_component(f, s2)
            //   v3 = get_facet_seed_connected_component(f, s3)
            //   if(v2 != NO_INDEX && v3 != NO_INDEX) push triangle
            auto const& tri  = tris[facet];
            auto const& va3D = liftedVerts[tri[0]];  // first 3 coords = 3D pos
            auto const& vb3D = liftedVerts[tri[1]];
            auto const& vc3D = liftedVerts[tri[2]];
            Real faceNx = (vb3D[1]-va3D[1])*(vc3D[2]-va3D[2]) - (vb3D[2]-va3D[2])*(vc3D[1]-va3D[1]);
            Real faceNy = (vb3D[2]-va3D[2])*(vc3D[0]-va3D[0]) - (vb3D[0]-va3D[0])*(vc3D[2]-va3D[2]);
            Real faceNz = (vb3D[0]-va3D[0])*(vc3D[1]-va3D[1]) - (vb3D[1]-va3D[1])*(vc3D[0]-va3D[0]);

            for (auto const& v : P.V)
            {
                if (v.nbBisectors != 2) { continue; }

                int32_t s2 = v.bisectors[0];
                int32_t s3 = v.bisectors[1];

                int32_t v1 = compID;
                int32_t v2 = rvd.GetFacetSeedComponent(facet, s2);
                int32_t v3 = rvd.GetFacetSeedComponent(facet, s3);

                // Skip if s2 or s3 haven't recorded their component on this facet
                // yet — the triangle will be re-emitted when a later seed processes
                // a facet where all three are available (matching Geogram's strategy).
                if (v2 < 0 || v3 < 0) { continue; }

                TriKey key = {v1, v2, v3};
                std::sort(key.begin(), key.end());
                if (key[0] == key[1] || key[1] == key[2]) { continue; }

                auto& na = candidates[key];
                na.nx += faceNx;
                na.ny += faceNy;
                na.nz += faceNz;
            }
        });

        FinalizeComp(prevComp);

        if (comps.empty() || candidates.empty())
        {
            return false;
        }

        // ── 3. Build all component centroids (indexed by component ID) ──────
        // Component IDs are used as vertex indices in the raw triangle list.
        // Ghost components (mass ≈ 0) produce a zero-position vertex that
        // will be removed by the postprocessing step below.
        std::vector<Vector3<Real>> rawVerts;
        rawVerts.reserve(comps.size());
        for (auto const& cd : comps)
        {
            rawVerts.push_back({cd.wx, cd.wy, cd.wz});
        }
        auto nRaw = static_cast<int32_t>(rawVerts.size());

        // ── 4. Build raw triangles with winding corrected by face normal ─────
        // Translation of the winding check in ComputeMultiNerveRDT.
        std::vector<std::array<int32_t,3>> rawTris;
        rawTris.reserve(candidates.size());

        for (auto const& kv : candidates)
        {
            int32_t a = kv.first[0];
            int32_t b = kv.first[1];
            int32_t c = kv.first[2];
            if (a >= nRaw || b >= nRaw || c >= nRaw) { continue; }

            auto const& na = kv.second;
            auto const& va = rawVerts[a];
            auto const& vb = rawVerts[b];
            auto const& vc = rawVerts[c];
            Real outNx = (vb[1]-va[1])*(vc[2]-va[2]) - (vb[2]-va[2])*(vc[1]-va[1]);
            Real outNy = (vb[2]-va[2])*(vc[0]-va[0]) - (vb[0]-va[0])*(vc[2]-va[2]);
            Real outNz = (vb[0]-va[0])*(vc[1]-va[1]) - (vb[1]-va[1])*(vc[0]-va[0]);

            Real dot = outNx * na.nx + outNy * na.ny + outNz * na.nz;
            if (dot >= Real(0))
            {
                rawTris.push_back({a, b, c});
            }
            else
            {
                rawTris.push_back({a, c, b});
            }
        }

        if (rawTris.empty())
        {
            return false;
        }

        // ── 5. PostprocessRDT — direct translation of Geogram's
        //        mesh_postprocess_RDT (mesh_repair.cpp).
        //
        // Step 5a: Remove degenerate triangles (same vertex twice) and
        //          duplicate triangles (same set of 3 vertices), matching
        //          Geogram's detect_bad_facets(check_duplicates=true).
        {
            // Canonicalize each triangle by sorting its vertex indices.
            // After sorting, degenerate triangles have key[0]==key[1] or key[1]==key[2].
            // Duplicate triangles share the same canonical key; keep only the first.
            std::map<std::array<int32_t,3>, bool> seen;
            auto end = std::remove_if(rawTris.begin(), rawTris.end(),
                [&](std::array<int32_t,3> const& tri) -> bool
                {
                    std::array<int32_t,3> key = tri;
                    std::sort(key.begin(), key.end());
                    // degenerate: any two vertices are the same
                    if (key[0] == key[1] || key[1] == key[2]) { return true; }
                    // duplicate: already seen this triangle
                    if (seen.count(key)) { return true; }
                    seen[key] = true;
                    return false;
                });
            rawTris.erase(end, rawTris.end());
        }

        // Step 5b: Iteratively remove "bad" triangles: any triangle where at least
        //          one vertex appears in only one non-bad triangle (isolated peninsula).
        //          Translation of the iterative detect_bad_facets loop in
        //          Geogram's mesh_postprocess_RDT.
        {
            bool changed = true;
            while (changed)
            {
                changed = false;
                std::vector<int32_t> inc(nRaw, 0);
                for (auto const& tri : rawTris)
                {
                    ++inc[tri[0]];
                    ++inc[tri[1]];
                    ++inc[tri[2]];
                }
                auto end = std::remove_if(rawTris.begin(), rawTris.end(),
                    [&](std::array<int32_t,3> const& tri) -> bool
                    {
                        if (inc[tri[0]] == 1 || inc[tri[1]] == 1 ||
                            inc[tri[2]] == 1)
                        {
                            changed = true;
                            return true;
                        }
                        return false;
                    });
                rawTris.erase(end, rawTris.end());
            }
        }

        if (rawTris.empty())
        {
            return false;
        }

        // Step 5c: Rebuild manifold adjacency, reorient triangles consistently
        //          (anti-Möbius BFS), split non-manifold vertices.
        //          Translation of the three Geogram repair functions called in
        //          mesh_postprocess_RDT after the initial peninsula removal:
        //            repair_connect_facets(M)
        //            repair_reorient_facets_anti_moebius(M)
        //            repair_split_non_manifold_vertices(M)
        {
            std::vector<int32_t> adj;
            RDTRepair::ConnectFacets(rawTris, adj);
            RDTRepair::ReorientFacetsAntiMoebius(rawVerts, rawTris, adj);
            RDTRepair::SplitNonManifoldVertices(rawVerts, rawTris, adj);

            // Update nRaw to reflect any vertices added by SplitNonManifoldVertices.
            nRaw = static_cast<int32_t>(rawVerts.size());
        }

        // Step 5d: Second peninsula-removal pass.
        // After the topology repair in Step 5c, SplitNonManifoldVertices may have
        // introduced new vertices with low triangle incidence (degree-1 vertices).
        // A second peninsula pass cleans up these isolated flaps, matching the
        // significant output reduction that Geogram achieves from its repair steps.
        {
            bool changed = true;
            while (changed)
            {
                changed = false;
                std::vector<int32_t> inc(nRaw, 0);
                for (auto const& tri : rawTris)
                {
                    ++inc[tri[0]];
                    ++inc[tri[1]];
                    ++inc[tri[2]];
                }
                auto end2 = std::remove_if(rawTris.begin(), rawTris.end(),
                    [&](std::array<int32_t,3> const& tri) -> bool
                    {
                        if (inc[tri[0]] == 1 || inc[tri[1]] == 1 ||
                            inc[tri[2]] == 1)
                        {
                            changed = true;
                            return true;
                        }
                        return false;
                    });
                rawTris.erase(end2, rawTris.end());
            }
        }

        if (rawTris.empty())
        {
            return false;
        }
        std::vector<int32_t> remap(nRaw, -1);
        int32_t nextV = 0;
        for (auto const& tri : rawTris)
        {
            for (int k = 0; k < 3; ++k)
            {
                if (remap[tri[k]] < 0)
                {
                    remap[tri[k]] = nextV++;
                }
            }
        }

        outVertices.resize(nextV);
        for (int32_t i = 0; i < nRaw; ++i)
        {
            if (remap[i] >= 0)
            {
                outVertices[remap[i]] = rawVerts[i];
            }
        }

        outTriangles.clear();
        outTriangles.reserve(rawTris.size());
        for (auto const& tri : rawTris)
        {
            outTriangles.push_back({remap[tri[0]], remap[tri[1]], remap[tri[2]]});
        }

        return true;
    }

}  // namespace gte
