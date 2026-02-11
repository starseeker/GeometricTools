// David Eberly, Geometric Tools, Redmond WA 98052
// Copyright (c) 1998-2026
// Distributed under the Boost Software License, Version 1.0.
// https://www.boost.org/LICENSE_1_0.txt
// https://www.geometrictools.com/License/Boost/LICENSE_1_0.txt
// File Version: 8.0.2026.02.11
//
// Co3Ne (Concurrent Co-Cones) ENHANCED surface reconstruction with full manifold extraction
//
// This version implements Geogram's full manifold extraction algorithm including:
// - Corner-based topology tracking
// - Connected component analysis  
// - Incremental triangle insertion with validation and rollback
// - Moebius strip detection
// - Orientation consistency enforcement
// - Non-manifold edge rejection
//
// Based on Geogram's Co3NeManifoldExtraction class
// (geogram/src/lib/geogram/points/co3ne.cpp lines 124-1240)

#pragma once

#include <GTE/Mathematics/Co3NeFull.h>
#include <chrono>

namespace gte
{
    template <typename Real>
    class Co3NeFullEnhanced : public Co3NeFull<Real>
    {
    public:
        using Vector3Type = Vector3<Real>;
        using Parameters = typename Co3NeFull<Real>::Parameters;
        
        // Enhanced parameters
        struct EnhancedParameters : public Parameters
        {
            bool useEnhancedManifold;   // Use full manifold extraction (default: true)
            // Note: strictMode is inherited from Parameters
            size_t maxManifoldIterations; // Max iterations for incremental insertion
            
            // NEW: Automatic manifold closure parameters
            bool guaranteeManifold;             // Enable automatic hole filling (default: true)
            double maxRefinementTimeSeconds;    // Max time for hole filling (default: 10.0 seconds)
            Real refinementRadiusMultiplier;    // Radius increase per iteration (default: 1.5)
            Real minTriangleQuality;            // Minimum quality for fill triangles (default: 0.1)
            
            // To set explicit parameters (like Geogram), use:
            //   params.searchRadius = 2.5;  // 0 = automatic (default)
            //   params.kNeighbors = 15;     // 0 = automatic (default)
            // When searchRadius > 0, guaranteeManifold is ignored (user controls behavior)
            
            EnhancedParameters() 
                : Parameters()
                , useEnhancedManifold(true)
                , maxManifoldIterations(50)
                , guaranteeManifold(true)
                , maxRefinementTimeSeconds(10.0)
                , refinementRadiusMultiplier(static_cast<Real>(1.5))
                , minTriangleQuality(static_cast<Real>(0.1))
            {
                // Can override inherited defaults here if needed
            }
        };
        
        // Main reconstruction with enhanced manifold
        static bool Reconstruct(
            std::vector<Vector3Type> const& points,
            std::vector<Vector3Type>& outVertices,
            std::vector<std::array<int32_t, 3>>& outTriangles,
            EnhancedParameters const& params = EnhancedParameters())
        {
            if (!params.useEnhancedManifold)
            {
                // Fall back to simplified version
                return Co3NeFull<Real>::Reconstruct(points, outVertices, outTriangles, params);
            }
            
            // Enhanced reconstruction pipeline
            if (points.empty())
            {
                return false;
            }

            // Step 1: Compute normals using PCA (from base class)
            std::vector<Vector3Type> normals;
            if (!Co3NeFull<Real>::ComputeNormals(points, normals, params))
            {
                return false;
            }

            // Step 2: Orient normals consistently if requested (from base class)
            if (params.orientNormals)
            {
                Co3NeFull<Real>::OrientNormalsConsistently(points, normals, params);
            }

            // Step 3: Generate candidate triangles using co-cone (from base class)
            std::vector<std::array<int32_t, 3>> candidateTriangles;
            Co3NeFull<Real>::GenerateTriangles(points, normals, candidateTriangles, params);

            if (candidateTriangles.empty())
            {
                return false;
            }

            // Step 4: Extract manifold using ENHANCED algorithm
            ManifoldExtractor extractor(points, normals, params);
            if (!extractor.Extract(candidateTriangles, outTriangles))
            {
                // Fall back to base class if enhanced fails
                Co3NeFull<Real>::ExtractManifold(points, normals, candidateTriangles, outTriangles, params);
            }

            if (outTriangles.empty())
            {
                return false;
            }

            // Step 4b: Automatic manifold closure (NEW)
            if (params.guaranteeManifold && params.searchRadius == static_cast<Real>(0))
            {
                // Only in automatic mode (not when user specifies explicit radius)
                Real initialRadius = Co3NeFull<Real>::ComputeAutomaticSearchRadius(points);
                RefineManifold(points, normals, outTriangles, initialRadius, params);
            }

            // Step 5: Return vertices (same as input points)
            outVertices = points;

            // Step 6: Optional RVD smoothing (from base class)
            if (params.smoothWithRVD && !outTriangles.empty())
            {
                Co3NeFull<Real>::SmoothWithRVD(outVertices, outTriangles, params);
            }

            return true;
        }
        
    private:
        // ===== AUTOMATIC MANIFOLD CLOSURE FUNCTIONS =====
        
        struct BoundaryInfo
        {
            std::vector<std::pair<int32_t, int32_t>> boundaryEdges;
            std::set<int32_t> boundaryVertices;
        };
        
        // Find boundary edges in a triangle mesh
        static BoundaryInfo FindBoundaryEdges(
            std::vector<std::array<int32_t, 3>> const& triangles)
        {
            BoundaryInfo info;
            std::map<std::pair<int32_t, int32_t>, int> edgeCount;
            
            // Count how many triangles use each edge
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
            
            // Find edges with only 1 triangle (boundary edges)
            for (auto const& [edge, count] : edgeCount)
            {
                if (count == 1)
                {
                    info.boundaryEdges.push_back(edge);
                    info.boundaryVertices.insert(edge.first);
                    info.boundaryVertices.insert(edge.second);
                }
            }
            
            return info;
        }
        
        // Check if adding a triangle would create a non-manifold edge
        static bool WouldCreateNonManifoldEdge(
            std::array<int32_t, 3> const& newTri,
            std::vector<std::array<int32_t, 3>> const& existingTriangles)
        {
            std::map<std::pair<int32_t, int32_t>, int> edgeCount;
            
            // Count edges in existing triangles
            for (auto const& tri : existingTriangles)
            {
                for (int i = 0; i < 3; ++i)
                {
                    auto edge = std::make_pair(
                        std::min(tri[i], tri[(i + 1) % 3]),
                        std::max(tri[i], tri[(i + 1) % 3]));
                    edgeCount[edge]++;
                }
            }
            
            // Check if new triangle's edges would exceed limit
            for (int i = 0; i < 3; ++i)
            {
                auto edge = std::make_pair(
                    std::min(newTri[i], newTri[(i + 1) % 3]),
                    std::max(newTri[i], newTri[(i + 1) % 3]));
                
                if (edgeCount[edge] >= 2)
                {
                    return true;  // Would have > 2 triangles on this edge
                }
            }
            
            return false;
        }
        
        // Check if triangle shares boundary edges (reduces hole size)
        static bool ReducesBoundaryCount(
            std::array<int32_t, 3> const& tri,
            BoundaryInfo const& boundaries)
        {
            int sharedBoundaryEdges = 0;
            
            for (int i = 0; i < 3; ++i)
            {
                auto edge = std::make_pair(
                    std::min(tri[i], tri[(i + 1) % 3]),
                    std::max(tri[i], tri[(i + 1) % 3]));
                
                if (std::find(boundaries.boundaryEdges.begin(),
                             boundaries.boundaryEdges.end(),
                             edge) != boundaries.boundaryEdges.end())
                {
                    sharedBoundaryEdges++;
                }
            }
            
            // Triangle reduces boundary if it closes a gap (shares >= 2 boundary edges)
            return sharedBoundaryEdges >= 2;
        }
        
        // Validate triangle quality
        static bool IsValidFillTriangle(
            std::array<int32_t, 3> const& tri,
            std::vector<Vector3Type> const& points,
            std::vector<Vector3Type> const& normals,
            EnhancedParameters const& params)
        {
            Vector3Type const& p0 = points[tri[0]];
            Vector3Type const& p1 = points[tri[1]];
            Vector3Type const& p2 = points[tri[2]];
            
            // Check for degenerate triangle
            Vector3Type e0 = p1 - p0;
            Vector3Type e1 = p2 - p1;
            Vector3Type e2 = p0 - p2;
            
            Real a = Length(e0);
            Real b = Length(e1);
            Real c = Length(e2);
            
            if (a < static_cast<Real>(1e-10) || 
                b < static_cast<Real>(1e-10) || 
                c < static_cast<Real>(1e-10))
            {
                return false;  // Degenerate
            }
            
            // Check triangle quality (inradius/circumradius ratio)
            Real s = (a + b + c) * static_cast<Real>(0.5);
            Real area = std::sqrt(s * (s - a) * (s - b) * (s - c));
            if (area < static_cast<Real>(1e-10))
            {
                return false;  // Degenerate
            }
            
            Real inradius = area / s;
            Real circumradius = (a * b * c) / (static_cast<Real>(4) * area);
            Real quality = inradius / circumradius;
            
            if (quality < params.minTriangleQuality)
            {
                return false;  // Too poor quality
            }
            
            // Check normal consistency
            Vector3Type triNormal = Cross(e0, -e2);
            Normalize(triNormal);
            Vector3Type avgNormal = normals[tri[0]] + normals[tri[1]] + normals[tri[2]];
            Normalize(avgNormal);
            
            if (Dot(triNormal, avgNormal) < static_cast<Real>(0))
            {
                return false;  // Wrong orientation
            }
            
            return true;
        }
        
        // Generate fill triangle candidates
        static std::vector<std::array<int32_t, 3>> GenerateFillTriangles(
            std::vector<Vector3Type> const& points,
            std::vector<Vector3Type> const& normals,
            BoundaryInfo const& boundaries,
            Real searchRadius,
            EnhancedParameters const& params)
        {
            std::vector<std::array<int32_t, 3>> candidates;
            
            // For each boundary edge, try to find a third vertex to close it
            for (auto const& [v0, v1] : boundaries.boundaryEdges)
            {
                Vector3Type const& p0 = points[v0];
                Vector3Type const& p1 = points[v1];
                Vector3Type edgeCenter = (p0 + p1) * static_cast<Real>(0.5);
                
                // Search for potential third vertices
                for (int32_t v2 : boundaries.boundaryVertices)
                {
                    if (v2 == v0 || v2 == v1) continue;
                    
                    Vector3Type const& p2 = points[v2];
                    
                    // Check if within search radius
                    Real dist = Length(p2 - edgeCenter);
                    if (dist > searchRadius) continue;
                    
                    // Try both orientations
                    std::array<int32_t, 3> tri1 = {v0, v1, v2};
                    std::array<int32_t, 3> tri2 = {v0, v2, v1};
                    
                    if (IsValidFillTriangle(tri1, points, normals, params))
                    {
                        candidates.push_back(tri1);
                    }
                    else if (IsValidFillTriangle(tri2, points, normals, params))
                    {
                        candidates.push_back(tri2);
                    }
                }
            }
            
            return candidates;
        }
        
        // Main refinement algorithm for automatic manifold closure
        static bool RefineManifold(
            std::vector<Vector3Type> const& points,
            std::vector<Vector3Type> const& normals,
            std::vector<std::array<int32_t, 3>>& inoutTriangles,
            Real initialRadius,
            EnhancedParameters const& params)
        {
            // Start timer
            auto startTime = std::chrono::steady_clock::now();
            
            Real currentRadius = initialRadius;
            size_t iteration = 0;
            
            // Keep trying until manifold achieved or timeout
            while (true)
            {
                // Check timeout
                auto currentTime = std::chrono::steady_clock::now();
                auto elapsed = std::chrono::duration<double>(currentTime - startTime).count();
                
                if (elapsed > params.maxRefinementTimeSeconds)
                {
                    // Timeout reached
                    return false;
                }
                
                // 1. Find boundary edges
                BoundaryInfo boundaries = FindBoundaryEdges(inoutTriangles);
                
                if (boundaries.boundaryEdges.empty())
                {
                    return true;  // Success - manifold achieved!
                }
                
                // 2. Increase search radius for this iteration
                currentRadius *= params.refinementRadiusMultiplier;
                
                // 3. Progressively relax quality threshold for later iterations
                // This helps close final difficult gaps
                Real currentQualityThreshold = params.minTriangleQuality;
                if (iteration > 5)
                {
                    // After 5 iterations, start relaxing quality
                    currentQualityThreshold /= (1.0 + (iteration - 5) * 0.2);
                }
                
                // Create modified params with relaxed quality
                EnhancedParameters relaxedParams = params;
                relaxedParams.minTriangleQuality = currentQualityThreshold;
                
                // 4. Generate fill triangle candidates
                auto candidates = GenerateFillTriangles(
                    points, normals, boundaries, currentRadius, relaxedParams);
                
                if (candidates.empty())
                {
                    iteration++;
                    continue;  // Try next iteration with larger radius
                }
                
                // 5. Add valid triangles that close holes
                size_t addedCount = 0;
                for (auto const& tri : candidates)
                {
                    // Check if would create non-manifold edge
                    if (WouldCreateNonManifoldEdge(tri, inoutTriangles))
                        continue;
                    
                    // Check if reduces boundary count
                    if (ReducesBoundaryCount(tri, boundaries))
                    {
                        inoutTriangles.push_back(tri);
                        addedCount++;
                    }
                }
                
                if (addedCount == 0)
                {
                    // No progress this iteration
                    iteration++;
                    continue;
                }
                
                iteration++;
            }
            
            // Should never reach here due to timeout or success
            return false;
        }
        
        // ===== END AUTOMATIC MANIFOLD CLOSURE FUNCTIONS =====
        
    private:
        // Enhanced manifold extractor (based on Geogram's Co3NeManifoldExtraction)
        class ManifoldExtractor
        {
        public:
            static constexpr int32_t NO_CORNER = -1;
            static constexpr int32_t NO_FACET = -1;
            static constexpr int32_t NO_CNX = -1;
            
            ManifoldExtractor(
                std::vector<Vector3Type> const& points,
                std::vector<Vector3Type> const& normals,
                EnhancedParameters const& params)
                : points_(points)
                , normals_(normals)
                , params_(params)
            {
            }
            
            // Extract manifold from candidate triangles
            bool Extract(
                std::vector<std::array<int32_t, 3>> const& candidateTriangles,
                std::vector<std::array<int32_t, 3>>& outTriangles)
            {
                if (candidateTriangles.empty())
                {
                    return false;
                }
                
                // Initialize with first triangle (seed)
                InitializeWithSeedTriangle(candidateTriangles);
                
                // Incrementally add remaining triangles
                AddTrianglesIncrementally(candidateTriangles);
                
                // Export result
                outTriangles = mesh_;
                return !mesh_.empty();
            }
            
        private:
            // Core data structures
            std::vector<Vector3Type> const& points_;
            std::vector<Vector3Type> const& normals_;
            EnhancedParameters const& params_;
            
            // Current mesh under construction
            std::vector<std::array<int32_t, 3>> mesh_;
            
            // Topology tracking (corner-based like Geogram)
            std::vector<int32_t> next_c_around_v_;  // Next corner around vertex (circular list)
            std::vector<int32_t> v2c_;              // Vertex to corner mapping
            std::vector<int32_t> adj_facet_;        // Adjacent facet per corner
            
            // Connected component tracking
            std::vector<int32_t> cnx_;              // Connected component per triangle
            std::vector<int32_t> cnx_size_;         // Size of each component
            int32_t num_components_;
            
            // Initialize with seed triangle
            void InitializeWithSeedTriangle(std::vector<std::array<int32_t, 3>> const& triangles)
            {
                // Take first triangle as seed
                auto const& seed = triangles[0];
                mesh_.push_back(seed);
                
                // Initialize data structures
                size_t numVertices = points_.size();
                v2c_.assign(numVertices, NO_CORNER);
                next_c_around_v_.assign(3, NO_CORNER);  // 3 corners for first triangle
                adj_facet_.assign(3, NO_FACET);
                
                // Insert seed triangle into topology
                Insert(0);
                
                // Initialize connected components
                cnx_.assign(1, 0);  // First triangle in component 0
                cnx_size_.assign(1, 1);
                num_components_ = 1;
            }
            
            // Incrementally add triangles
            void AddTrianglesIncrementally(std::vector<std::array<int32_t, 3>> const& triangles)
            {
                std::vector<bool> added(triangles.size(), false);
                added[0] = true;  // Seed already added
                
                bool changed = true;
                size_t iter = 0;
                
                while (changed && iter < params_.maxManifoldIterations)
                {
                    changed = false;
                    ++iter;
                    
                    for (size_t i = 1; i < triangles.size(); ++i)
                    {
                        if (added[i])
                        {
                            continue;
                        }
                        
                        auto const& tri = triangles[i];
                        
                        // Try to add triangle
                        int32_t t_idx = AddTriangle(tri[0], tri[1], tri[2]);
                        
                        bool classified = false;
                        if (ConnectAndValidateTriangle(t_idx, classified))
                        {
                            // Triangle accepted
                            added[i] = true;
                            changed = true;
                        }
                        else
                        {
                            // Triangle rejected, rollback
                            RollbackTriangle();
                        }
                    }
                }
            }
            
            // Add triangle to mesh
            int32_t AddTriangle(int32_t i, int32_t j, int32_t k)
            {
                std::array<int32_t, 3> tri = {i, j, k};
                mesh_.push_back(tri);
                
                int32_t t_idx = static_cast<int32_t>(mesh_.size()) - 1;
                
                // Extend topology arrays
                next_c_around_v_.push_back(NO_CORNER);
                next_c_around_v_.push_back(NO_CORNER);
                next_c_around_v_.push_back(NO_CORNER);
                
                adj_facet_.push_back(NO_FACET);
                adj_facet_.push_back(NO_FACET);
                adj_facet_.push_back(NO_FACET);
                
                // Insert into topology
                Insert(t_idx);
                
                return t_idx;
            }
            
            // Rollback last triangle
            void RollbackTriangle()
            {
                if (mesh_.empty())
                {
                    return;
                }
                
                int32_t t = static_cast<int32_t>(mesh_.size()) - 1;
                
                // Remove from topology
                Remove(t, true);
                
                // Remove from mesh
                mesh_.pop_back();
                
                // Remove from topology arrays
                next_c_around_v_.pop_back();
                next_c_around_v_.pop_back();
                next_c_around_v_.pop_back();
                
                adj_facet_.pop_back();
                adj_facet_.pop_back();
                adj_facet_.pop_back();
                
                // Remove from connected components if present
                if (static_cast<size_t>(t) < cnx_.size())
                {
                    int32_t comp = cnx_[t];
                    if (comp != NO_CNX && comp < static_cast<int32_t>(cnx_size_.size()))
                    {
                        --cnx_size_[comp];
                    }
                    cnx_.pop_back();
                }
            }
            
            // Connect and validate triangle
            bool ConnectAndValidateTriangle(int32_t t, bool& classified)
            {
                classified = false;
                
                // Get adjacent corners
                int32_t adj_c[3];
                if (!GetAdjacentCorners(t, adj_c))
                {
                    classified = true;
                    return false;  // Non-manifold edges
                }
                
                // Geometric test: Check normal agreement
                for (int i = 0; i < 3; ++i)
                {
                    if (adj_c[i] != NO_CORNER)
                    {
                        int32_t t2 = CornerToFacet(adj_c[i]);
                        if (!TrianglesNormalsAgree(t, t2))
                        {
                            classified = true;
                            return false;
                        }
                    }
                }
                
                // Count neighbors
                int nb_neighbors = 0;
                for (int i = 0; i < 3; ++i)
                {
                    if (adj_c[i] != NO_CORNER)
                    {
                        ++nb_neighbors;
                    }
                }
                
                // Combinatorial test: Need at least one neighbor
                if (nb_neighbors == 0)
                {
                    return false;  // Isolated triangle
                }
                
                // In strict mode, require at least 2 neighbors
                if (params_.strictMode && nb_neighbors == 1)
                {
                    return false;  // Strict mode rejects boundary edges
                }
                
                // Connect adjacent corners
                ConnectAdjacentCorners(t, adj_c);
                
                // Check for non-manifold vertices
                for (int i = 0; i < 3; ++i)
                {
                    int32_t v = mesh_[t][i];
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
                
                // Enforce orientation consistency
                if (!EnforceOrientationFromTriangle(t))
                {
                    return false;  // Would create Moebius strip
                }
                
                classified = true;
                return true;
            }
            
            // Insert triangle into topology
            void Insert(int32_t t)
            {
                for (int i = 0; i < 3; ++i)
                {
                    int32_t c = t * 3 + i;
                    int32_t v = mesh_[t][i];
                    
                    if (v2c_[v] == NO_CORNER)
                    {
                        v2c_[v] = c;
                        next_c_around_v_[c] = c;  // Points to itself
                    }
                    else
                    {
                        // Insert into circular list
                        next_c_around_v_[c] = next_c_around_v_[v2c_[v]];
                        next_c_around_v_[v2c_[v]] = c;
                    }
                }
            }
            
            // Remove triangle from topology
            void Remove(int32_t t, bool disconnect)
            {
                // Disconnect from neighbors if requested
                if (disconnect)
                {
                    for (int i = 0; i < 3; ++i)
                    {
                        int32_t c = t * 3 + i;
                        int32_t adj_t = adj_facet_[c];
                        
                        if (adj_t != NO_FACET)
                        {
                            // Find corresponding corner in adjacent triangle
                            for (int j = 0; j < 3; ++j)
                            {
                                int32_t adj_c = adj_t * 3 + j;
                                if (adj_facet_[adj_c] == t)
                                {
                                    adj_facet_[adj_c] = NO_FACET;
                                }
                            }
                        }
                    }
                }
                
                // Remove from circular lists
                for (int i = 0; i < 3; ++i)
                {
                    int32_t c = t * 3 + i;
                    int32_t v = mesh_[t][i];
                    
                    if (next_c_around_v_[c] == c)
                    {
                        // Only corner for this vertex
                        v2c_[v] = NO_CORNER;
                    }
                    else
                    {
                        // Find predecessor
                        int32_t c_pred = next_c_around_v_[c];
                        while (next_c_around_v_[c_pred] != c)
                        {
                            c_pred = next_c_around_v_[c_pred];
                        }
                        next_c_around_v_[c_pred] = next_c_around_v_[c];
                        v2c_[v] = c_pred;
                    }
                }
            }
            
            // Get adjacent corners for triangle
            bool GetAdjacentCorners(int32_t t1, int32_t adj_c[3])
            {
                for (int i = 0; i < 3; ++i)
                {
                    adj_c[i] = NO_CORNER;
                    
                    int32_t c1 = t1 * 3 + i;
                    int32_t v1 = mesh_[t1][i];
                    int32_t v2 = mesh_[t1][(i + 1) % 3];
                    
                    // Traverse corners around v1
                    int32_t c2 = next_c_around_v_[c1];
                    while (c2 != c1)
                    {
                        int32_t t2 = CornerToFacet(c2);
                        
                        // Check if this triangle shares the edge (v1, v2)
                        for (int j = 0; j < 3; ++j)
                        {
                            int32_t w1 = mesh_[t2][j];
                            int32_t w2 = mesh_[t2][(j + 1) % 3];
                            
                            if ((w1 == v1 && w2 == v2) || (w1 == v2 && w2 == v1))
                            {
                                if (adj_c[i] == NO_CORNER)
                                {
                                    adj_c[i] = t2 * 3 + j;
                                }
                                else
                                {
                                    // Non-manifold edge
                                    return false;
                                }
                            }
                        }
                        
                        c2 = next_c_around_v_[c2];
                    }
                }
                
                return true;
            }
            
            // Connect adjacent corners
            void ConnectAdjacentCorners(int32_t t, int32_t adj_c[3])
            {
                for (int i = 0; i < 3; ++i)
                {
                    if (adj_c[i] != NO_CORNER)
                    {
                        int32_t c = t * 3 + i;
                        int32_t adj_t = CornerToFacet(adj_c[i]);
                        
                        adj_facet_[c] = adj_t;
                        adj_facet_[adj_c[i]] = t;
                    }
                }
            }
            
            // Check if vertex is non-manifold by excess
            bool VertexIsNonManifoldByExcess(int32_t v, bool& moebius)
            {
                moebius = false;
                
                if (v2c_[v] == NO_CORNER)
                {
                    return false;
                }
                
                int32_t nb_triangles = 0;
                int32_t c = v2c_[v];
                do
                {
                    ++nb_triangles;
                    c = next_c_around_v_[c];
                } while (c != v2c_[v]);
                
                // Check for closed loops
                c = v2c_[v];
                do
                {
                    int32_t loop_size = 0;
                    int32_t c_cur = c;
                    
                    do
                    {
                        ++loop_size;
                        if (c_cur == NO_CORNER)
                        {
                            break;
                        }
                        if (loop_size > 100)
                        {
                            moebius = true;
                            break;
                        }
                        c_cur = NextAroundVertexUnoriented(v, c_cur);
                    } while (c_cur != c);
                    
                    if (c_cur == c && loop_size < nb_triangles)
                    {
                        return true;  // Non-manifold by excess
                    }
                    
                    c = next_c_around_v_[c];
                } while (c != v2c_[v]);
                
                return false;
            }
            
            // Get next corner around vertex (unoriented)
            int32_t NextAroundVertexUnoriented(int32_t v, int32_t c1) const
            {
                int32_t t1 = CornerToFacet(c1);
                int32_t c1_in_t1 = c1 - t1 * 3;
                
                int32_t v1 = mesh_[t1][c1_in_t1];
                int32_t v2 = mesh_[t1][(c1_in_t1 + 1) % 3];
                
                int32_t adj_t = adj_facet_[c1];
                if (adj_t == NO_FACET)
                {
                    return NO_CORNER;
                }
                
                // Find matching edge in adjacent triangle
                for (int i = 0; i < 3; ++i)
                {
                    int32_t w1 = mesh_[adj_t][i];
                    int32_t w2 = mesh_[adj_t][(i + 1) % 3];
                    
                    if ((v1 == w1 && v2 == w2) || (v1 == w2 && v2 == w1))
                    {
                        if (w2 == v)
                        {
                            return adj_t * 3 + ((i + 1) % 3);
                        }
                        else
                        {
                            return adj_t * 3 + ((i + 2) % 3);
                        }
                    }
                }
                
                return NO_CORNER;
            }
            
            // Enforce orientation consistency from triangle
            bool EnforceOrientationFromTriangle(int32_t t)
            {
                // Get adjacent triangles and their orientations
                int32_t adj[3];
                int32_t adj_cnx[3];
                int32_t adj_ori[3];
                
                for (int i = 0; i < 3; ++i)
                {
                    int32_t c = t * 3 + i;
                    adj[i] = adj_facet_[c];
                    
                    if (adj[i] == NO_FACET)
                    {
                        adj_ori[i] = 0;
                        adj_cnx[i] = NO_CNX;
                    }
                    else
                    {
                        adj_ori[i] = TrianglesHaveSameOrientation(t, adj[i]) ? 1 : -1;
                        adj_cnx[i] = (adj[i] < static_cast<int32_t>(cnx_.size())) ? cnx_[adj[i]] : NO_CNX;
                    }
                }
                
                // Check for Moebius strips
                for (int i = 0; i < 3; ++i)
                {
                    if (adj[i] != NO_FACET)
                    {
                        for (int j = i + 1; j < 3; ++j)
                        {
                            if (adj_cnx[j] == adj_cnx[i] && adj_ori[j] != adj_ori[i])
                            {
                                return false;  // Would create Moebius strip
                            }
                        }
                    }
                }
                
                // Find largest component
                int32_t largest_comp = NO_CNX;
                for (int i = 0; i < 3; ++i)
                {
                    if (adj_cnx[i] != NO_CNX)
                    {
                        if (largest_comp == NO_CNX || 
                            cnx_size_[adj_cnx[i]] > cnx_size_[adj_cnx[largest_comp]])
                        {
                            largest_comp = i;
                        }
                    }
                }
                
                if (largest_comp == NO_CNX)
                {
                    // Create new component
                    int32_t new_comp = num_components_++;
                    cnx_.resize(std::max(static_cast<size_t>(t + 1), cnx_.size()), NO_CNX);
                    cnx_[t] = new_comp;
                    cnx_size_.resize(num_components_, 0);
                    cnx_size_[new_comp] = 1;
                    return true;
                }
                
                // Add to largest component
                int32_t comp = adj_cnx[largest_comp];
                cnx_.resize(std::max(static_cast<size_t>(t + 1), cnx_.size()), NO_CNX);
                cnx_[t] = comp;
                ++cnx_size_[comp];
                
                // Flip if necessary
                if (adj_ori[largest_comp] == -1)
                {
                    FlipTriangle(t);
                }
                
                return true;
            }
            
            // Check if two triangles have same orientation
            bool TrianglesHaveSameOrientation(int32_t t1, int32_t t2) const
            {
                int32_t i1 = mesh_[t1][0];
                int32_t j1 = mesh_[t1][1];
                int32_t k1 = mesh_[t1][2];
                
                int32_t i2 = mesh_[t2][0];
                int32_t j2 = mesh_[t2][1];
                int32_t k2 = mesh_[t2][2];
                
                // Check all orientations that would be "same"
                if ((i1 == i2 && j1 == j2) || (i1 == k2 && j1 == i2) || (i1 == j2 && j1 == k2) ||
                    (k1 == k2 && i1 == i2) || (k1 == j2 && i1 == k2) || (k1 == i2 && i1 == j2) ||
                    (j1 == j2 && k1 == k2) || (j1 == i2 && k1 == j2) || (j1 == k2 && k1 == i2))
                {
                    return false;  // Opposite orientation
                }
                
                return true;
            }
            
            // Flip triangle orientation
            void FlipTriangle(int32_t t)
            {
                // Remove from topology
                Remove(t, false);
                
                // Swap vertices
                std::swap(mesh_[t][0], mesh_[t][2]);
                
                // Re-insert
                Insert(t);
            }
            
            // Check if triangles normals agree
            bool TrianglesNormalsAgree(int32_t t1, int32_t t2) const
            {
                Vector3<Real> n1 = ComputeTriangleNormal(t1);
                Vector3<Real> n2 = ComputeTriangleNormal(t2);
                
                Real dot = Dot(n1, n2);
                Real angle = std::acos(std::clamp(dot, Real(-1), Real(1)));
                Real maxAngle = params_.maxNormalAngle * static_cast<Real>(3.14159265358979323846 / 180.0);
                
                return angle <= maxAngle;
            }
            
            // Compute triangle normal
            Vector3<Real> ComputeTriangleNormal(int32_t t) const
            {
                Vector3<Real> const& p0 = points_[mesh_[t][0]];
                Vector3<Real> const& p1 = points_[mesh_[t][1]];
                Vector3<Real> const& p2 = points_[mesh_[t][2]];
                
                Vector3<Real> e1 = p1 - p0;
                Vector3<Real> e2 = p2 - p0;
                Vector3<Real> normal = Cross(e1, e2);
                Normalize(normal);
                return normal;
            }
            
            // Convert corner index to facet index
            int32_t CornerToFacet(int32_t c) const
            {
                return c / 3;
            }
        };
    };
}
