// David Eberly, Geometric Tools, Redmond WA 98052
// Copyright (c) 1998-2026
// Distributed under the Boost Software License, Version 1.0.
// https://www.boost.org/LICENSE_1_0.txt
// https://www.geometrictools.com/License/Boost/LICENSE_1_0.txt
// File Version: 8.0.2026.02.26
//
// Ported from Geogram: https://github.com/BrunoLevy/geogram
// Original files: src/lib/geogram/mesh/mesh_fill_holes.cpp, mesh_fill_holes.h
// Original license: BSD 3-Clause
// Copyright (c) 2000-2022 Inria
//
// Adaptations for Geometric Tools Engine:
// - Changed from GEO::Mesh to std::vector<Vector3<Real>> and
//   std::vector<std::array<int32_t, 3>>
// - Uses GTE's TriangulateEC and TriangulateCDT for robust triangulation
// - Removed Geogram-specific Logger and CmdLine calls
//
// WINDING-ORDER NOTES
// For a consistently oriented mesh (CCW outward normals), boundary loops
// traced by following the mesh's directed edges are CW (inner holes) or CCW
// (outer perimeter of an open mesh).  Only CW loops represent true holes that
// should be filled; CCW loops are skipped.
//
// A boundary loop is classified as CW when the dot product of its
// un-normalized Newell area vector and the average mesh outward normal is
// negative.
//
// For a CW hole boundary projected to 2D via its Newell normal (which always
// yields a CCW 2D polygon), the fill triangles from TriangulateEC / CDT are
// CCW in 2D.  When mapped back to 3D using the original CW vertex order,
// the winding must be reversed (swap the last two indices) to produce
// outward-facing (CCW) 3D triangles.
//
// For the 3D ear-cutting path the same reversal is achieved by using the
// formula {before, after, ear_tip} for loop triangles and
// {v0, v2, v1} for the final 3-vertex triangle.

#pragma once

#include <Mathematics/ArbitraryPrecision.h>
#include <Mathematics/MeshRepair.h>
#include <Mathematics/MeshValidation.h>
#include <Mathematics/Polygon2Validation.h>
#include <Mathematics/PolygonTree.h>
#include <Mathematics/TriangulateCDT.h>
#include <Mathematics/TriangulateEC.h>
#include <Mathematics/Vector2.h>
#include <Mathematics/Vector3.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <numeric>
#include <set>
#include <vector>

namespace gte
{
    template <typename Real>
    class MeshHoleFilling
    {
    public:
        // Triangulation method for hole filling.
        enum class TriangulationMethod
        {
            LoopSplit,      // Geogram default: recursive split by min chord/arc ratio
            EarClipping,    // GTE TriangulateEC with exact arithmetic (2D projection)
            CDT,            // GTE TriangulateCDT with exact arithmetic (2D projection)
            EarClipping3D   // Direct 3D ear-cutting (handles non-planar holes)
        };

        // Parameters for hole filling operations.
        struct Parameters
        {
            // Maximum hole area to fill (0 = fill all holes regardless of area).
            Real maxArea;

            // Maximum number of boundary edges (0 = unlimited).
            size_t maxEdges;

            // Run MeshRepair after filling.
            bool repair;

            // Triangulation algorithm to use.
            TriangulationMethod method;

            // Maximum planarity deviation (relative to hole diameter) above which
            // the 3D method is automatically preferred.  0 = disabled.
            Real planarityThreshold;

            // Automatically fall back to EarClipping3D if the chosen 2D method
            // fails (e.g. self-intersecting projection).
            bool autoFallback;

            // Validate the output after filling.
            bool validateOutput;
            bool requireManifold;
            bool requireNoSelfIntersections;

            Parameters()
                : maxArea(static_cast<Real>(0))
                , maxEdges(0)           // 0 = unlimited
                , repair(true)
                , method(TriangulationMethod::LoopSplit)
                , planarityThreshold(static_cast<Real>(0))
                , autoFallback(true)
                , validateOutput(true)
                , requireManifold(true)
                , requireNoSelfIntersections(false)
            {
            }
        };

        // A boundary loop that represents a single hole.
        struct HoleBoundary
        {
            std::vector<int32_t> vertices;  // Ordered vertex indices
            Real area;                      // Approximate post-fill area

            HoleBoundary()
                : area(static_cast<Real>(0))
            {
            }
        };

        // Fill all inner holes in the mesh that satisfy the size constraints.
        // Outer boundary loops of open meshes are skipped automatically.
        static void FillHoles(
            std::vector<Vector3<Real>>& vertices,
            std::vector<std::array<int32_t, 3>>& triangles,
            Parameters const& params = Parameters())
        {
            // Build directed-edge → triangle(s) map.
            std::map<std::pair<int32_t, int32_t>, std::vector<size_t>>
                edgeToTriangles;
            for (size_t i = 0; i < triangles.size(); ++i)
            {
                auto const& tri = triangles[i];
                for (int j = 0; j < 3; ++j)
                {
                    int32_t v0 = tri[j];
                    int32_t v1 = tri[(j + 1) % 3];
                    edgeToTriangles[{v0, v1}].push_back(i);
                }
            }

            // Detect all boundary loops.
            std::vector<HoleBoundary> holes;
            DetectHolesFromEdges(edgeToTriangles, holes);
            if (holes.empty())
            {
                return;
            }

            // Compute the average outward normal of the mesh.  This is used to
            // classify each boundary loop as CW (inner hole) or CCW (outer
            // perimeter of an open mesh).  Only CW loops are filled.
            Vector3<Real> avgMeshNormal =
                ComputeAverageMeshNormal(vertices, triangles);
            bool hasMeshNormal =
                (Length(avgMeshNormal) > static_cast<Real>(1e-10));

            size_t numFilled = 0;
            for (auto const& hole : holes)
            {
                // Apply edge-count constraint (0 = no constraint).
                if (params.maxEdges > 0 &&
                    hole.vertices.size() > params.maxEdges)
                {
                    continue;
                }

                // Classify boundary orientation.  A CW loop is an inner hole;
                // a CCW loop is the outer perimeter of an open mesh.
                if (hasMeshNormal)
                {
                    Vector3<Real> rawNewell =
                        ComputeHoleNormalRaw(vertices, hole);
                    // Negative dot → the boundary normal opposes the outward
                    // mesh normal → CW boundary → inner hole → fill.
                    // Positive dot → CCW boundary → outer perimeter → skip.
                    if (Dot(rawNewell, avgMeshNormal) >= static_cast<Real>(0))
                    {
                        continue;
                    }
                }

                // Determine which triangulation method to use.
                TriangulationMethod actualMethod = params.method;
                if (params.planarityThreshold > static_cast<Real>(0) &&
                    (actualMethod == TriangulationMethod::EarClipping ||
                     actualMethod == TriangulationMethod::CDT ||
                     actualMethod == TriangulationMethod::LoopSplit))
                {
                    Vector3<Real> normal = ComputeHoleNormal(vertices, hole);
                    Real planarity =
                        ComputeHolePlanarity(vertices, hole, normal);
                    Real holeSize = EstimateHoleSize(vertices, hole);
                    Real relDev = (holeSize > static_cast<Real>(0))
                        ? planarity / holeSize : planarity;
                    if (relDev > params.planarityThreshold)
                    {
                        actualMethod = TriangulationMethod::EarClipping3D;
                    }
                }

                std::vector<std::array<int32_t, 3>> newTriangles;
                bool success = false;

                if (actualMethod == TriangulationMethod::EarClipping3D)
                {
                    success = TriangulateHole3D(vertices, hole, newTriangles);
                }
                else if (actualMethod == TriangulationMethod::LoopSplit)
                {
                    success = TriangulateHoleLoopSplit(vertices, hole,
                        newTriangles);
                    if (!success && params.autoFallback)
                    {
                        success = TriangulateHole3D(vertices, hole,
                            newTriangles);
                    }
                }
                else
                {
                    success = TriangulateHole(vertices, hole, newTriangles,
                        actualMethod);
                    if (!success && params.autoFallback)
                    {
                        success = TriangulateHole3D(vertices, hole, newTriangles);
                    }
                }

                if (!success)
                {
                    continue;
                }

                // Apply area constraint (0 = no constraint).
                Real holeArea = ComputeTriangulationArea(vertices, newTriangles);
                if (params.maxArea > static_cast<Real>(0) &&
                    holeArea >= params.maxArea)
                {
                    continue;
                }

                triangles.insert(triangles.end(),
                    newTriangles.begin(), newTriangles.end());
                ++numFilled;
            }

            // Optional post-repair.
            if (numFilled > 0 && params.repair)
            {
                typename MeshRepair<Real>::Parameters repairParams;
                repairParams.mode = MeshRepair<Real>::RepairMode::DEFAULT;
                MeshRepair<Real>::Repair(vertices, triangles, repairParams);
            }
        }

    private:
        // ---------------------------------------------------------------------------
        // Hole detection

        static void DetectHolesFromEdges(
            std::map<std::pair<int32_t, int32_t>,
                std::vector<size_t>> const& edgeToTriangles,
            std::vector<HoleBoundary>& holes)
        {
            std::set<std::pair<int32_t, int32_t>> visitedEdges;

            for (auto const& entry : edgeToTriangles)
            {
                int32_t v0 = entry.first.first;
                int32_t v1 = entry.first.second;

                // A boundary edge has no matching reverse edge.
                if (edgeToTriangles.count({v1, v0}) > 0)
                {
                    continue;
                }
                if (visitedEdges.count(entry.first) > 0)
                {
                    continue;
                }

                HoleBoundary hole;
                if (TraceHoleBoundary(edgeToTriangles, v0, v1,
                    visitedEdges, hole) && hole.vertices.size() >= 3)
                {
                    holes.push_back(hole);
                }
            }
        }

        // Trace a boundary loop starting from directed edge (startV0 → startV1).
        static bool TraceHoleBoundary(
            std::map<std::pair<int32_t, int32_t>,
                std::vector<size_t>> const& edgeToTriangles,
            int32_t startV0,
            int32_t startV1,
            std::set<std::pair<int32_t, int32_t>>& visitedEdges,
            HoleBoundary& hole)
        {
            int32_t currentV = startV0;
            int32_t nextV    = startV1;
            int const maxIterations = 100000;
            int iterations = 0;

            do
            {
                hole.vertices.push_back(currentV);
                visitedEdges.insert({currentV, nextV});

                int32_t prevV = currentV;
                currentV = nextV;
                nextV = FindNextBoundaryVertex(
                    edgeToTriangles, prevV, currentV);

                if (nextV == -1)
                {
                    return false;
                }
                if (++iterations > maxIterations)
                {
                    return false;
                }
            } while (currentV != startV0);

            return true;
        }

        // Find the next boundary vertex continuing from (prevV → currentV).
        static int32_t FindNextBoundaryVertex(
            std::map<std::pair<int32_t, int32_t>,
                std::vector<size_t>> const& edgeToTriangles,
            int32_t prevV,
            int32_t currentV)
        {
            // Use lower_bound to find all edges starting at currentV
            // in O(log E + deg(currentV)) instead of O(E).
            auto lo = edgeToTriangles.lower_bound(
                {currentV, std::numeric_limits<int32_t>::lowest()});
            for (auto it = lo;
                 it != edgeToTriangles.end() && it->first.first == currentV;
                 ++it)
            {
                int32_t v1 = it->first.second;
                if (v1 != prevV && edgeToTriangles.count({v1, currentV}) == 0)
                {
                    return v1;
                }
            }
            return -1;
        }

        // ---------------------------------------------------------------------------
        // Loop-split triangulation (matches Geogram's default LOOP_SPLIT algorithm)

        // Triangulate a CW hole boundary using Geogram's loop-split algorithm.
        // Recursively bisects the hole by finding the chord (vi, vj) that
        // minimises rij = dxij / dsij, where dxij is the Euclidean distance
        // between the two vertices and dsij = min(arc_i_to_j, arc_j_to_i) is
        // the shorter arc length along the boundary.  This reproduces the
        // behaviour of geogram's triangulate_hole_loop_splitting().
        //
        // The hole vertices are in CW order (inner hole of a CCW-outward mesh).
        // The base-case triangle is stored with reversed winding so that the
        // fill triangles are CCW (outward-facing).
        static bool TriangulateHoleLoopSplit(
            std::vector<Vector3<Real>> const& vertices,
            HoleBoundary const& hole,
            std::vector<std::array<int32_t, 3>>& triangles)
        {
            size_t n = hole.vertices.size();
            if (n < 3)
            {
                return false;
            }

            if (n == 3)
            {
                // Reverse CW boundary vertices to produce a CCW fill triangle.
                triangles.push_back(
                {
                    hole.vertices[2],
                    hole.vertices[1],
                    hole.vertices[0]
                });
                return true;
            }

            // Compute cumulative arc lengths along the boundary.
            std::vector<Real> s(n);
            s[0] = static_cast<Real>(0);
            for (size_t i = 1; i < n; ++i)
            {
                s[i] = s[i - 1] + Length(
                    vertices[hole.vertices[i]] -
                    vertices[hole.vertices[i - 1]]);
            }
            Real totalLength = s[n - 1] + Length(
                vertices[hole.vertices[0]] -
                vertices[hole.vertices[n - 1]]);

            // Find the non-adjacent pair (bestI, bestJ) with minimum
            // rij = max(dxij, eps) / max(dsij, eps).
            // This is an O(n²) search, matching the original geogram algorithm
            // (geogram's split_hole() is also O(n²) with no spatial acceleration).
            Real bestRij = std::numeric_limits<Real>::max();
            int32_t bestI = -1;
            int32_t bestJ = -1;

            for (size_t i = 0; i < n; ++i)
            {
                for (size_t j = i + 2; j < n; ++j)
                {
                    // Skip the implicit closing edge (last → first vertex).
                    if (i == 0 && j == n - 1)
                    {
                        continue;
                    }

                    Real arcFwd = s[j] - s[i];
                    Real dsij = std::min(arcFwd, totalLength - arcFwd);
                    Real dxij = Length(
                        vertices[hole.vertices[j]] -
                        vertices[hole.vertices[i]]);

                    // Clamp to 1e-6 to avoid division by zero or instability
                    // when vertices are coincident or the arc is degenerate.
                    // The value 1e-6 matches geogram's split_hole() guard.
                    dsij = std::max(dsij, static_cast<Real>(1e-6));
                    dxij = std::max(dxij, static_cast<Real>(1e-6));
                    Real rij = dxij / dsij;

                    if (rij < bestRij)
                    {
                        bestRij = rij;
                        bestI = static_cast<int32_t>(i);
                        bestJ = static_cast<int32_t>(j);
                    }
                }
            }

            if (bestI < 0 || bestJ < 0)
            {
                return false;
            }

            // Split the hole into two sub-holes:
            //   hole1 = vertices[0..bestI] + vertices[bestJ..n-1]
            //   hole2 = vertices[bestI..bestJ]
            // Both sub-holes are CW (same orientation as the original).
            HoleBoundary hole1, hole2;
            for (int32_t i = 0; i < static_cast<int32_t>(n); ++i)
            {
                if (i <= bestI || i >= bestJ)
                {
                    hole1.vertices.push_back(hole.vertices[i]);
                }
                if (i >= bestI && i <= bestJ)
                {
                    hole2.vertices.push_back(hole.vertices[i]);
                }
            }

            bool ok = TriangulateHoleLoopSplit(vertices, hole1, triangles);
            ok = ok && TriangulateHoleLoopSplit(vertices, hole2, triangles);
            return ok;
        }

        // ---------------------------------------------------------------------------
        // 2-D triangulation (projection-based)

        // Triangulate a CW hole boundary by projecting it to 2D.
        //
        // The Newell normal of a CW boundary points inward (opposite to the mesh's
        // outward normals).  Projecting onto the Newell plane always yields a CCW
        // 2D polygon.  TriangulateEC / CDT produce CCW 2D triangles.  When mapped
        // back to 3D via the ORIGINAL (CW) vertex index order, those CCW triangles
        // become CW.  Swapping the last two indices in each fill triangle reverses
        // the winding to CCW (outward-facing), matching the surrounding mesh.
        static bool TriangulateHole(
            std::vector<Vector3<Real>> const& vertices,
            HoleBoundary const& hole,
            std::vector<std::array<int32_t, 3>>& triangles,
            TriangulationMethod method)
        {
            if (hole.vertices.size() < 3)
            {
                return false;
            }

            Vector3<Real> normal = ComputeHoleNormal(vertices, hole);
            if (Length(normal) < static_cast<Real>(1e-10))
            {
                return false;
            }

            Vector3<Real> uAxis, vAxis;
            ComputeTangentSpace(normal, uAxis, vAxis);

            std::vector<Vector2<Real>> points2D;
            points2D.reserve(hole.vertices.size());
            for (int32_t idx : hole.vertices)
            {
                Vector3<Real> const& p = vertices[idx];
                points2D.push_back({Dot(p, uAxis), Dot(p, vAxis)});
            }

            // Reject if the projected polygon self-intersects; the auto-fallback
            // in FillHoles will then attempt the 3D method.
            if (Polygon2Validation<Real>::HasSelfIntersectingEdges(points2D))
            {
                return false;
            }

            // Triangulate the CCW 2D polygon.
            std::vector<std::array<int32_t, 3>> localTriangles;
            bool ok = false;
            if (method == TriangulationMethod::EarClipping)
            {
                ok = TriangulateWithEC(points2D, localTriangles);
            }
            else
            {
                ok = TriangulateWithCDT(points2D, localTriangles);
            }

            if (!ok)
            {
                return false;
            }

            // Map 2D local indices back to global 3D vertex indices.
            // Swap the last two indices of each triangle to reverse the winding:
            // the projected polygon is CCW so its triangles are CCW in 2D, but
            // when indexed through the original CW hole.vertices they become CW
            // in 3D.  The swap makes them CCW (outward-facing).
            for (auto const& tri : localTriangles)
            {
                triangles.push_back(
                {
                    hole.vertices[tri[0]],
                    hole.vertices[tri[2]],  // swapped
                    hole.vertices[tri[1]]   // swapped
                });
            }

            return true;
        }

        // Triangulate using GTE's exact-arithmetic Ear Clipping.
        static bool TriangulateWithEC(
            std::vector<Vector2<Real>> const& points2D,
            std::vector<std::array<int32_t, 3>>& triangles)
        {
            try
            {
                // UIntegerAP32 provides arbitrary precision for both float and
                // double inputs (unlike UIntegerFP32<N> which is input-type
                // specific).
                using ComputeType = BSNumber<UIntegerAP32>;

                TriangulateEC<Real, ComputeType> triangulator(points2D);
                triangulator();
                triangles = triangulator.GetTriangles();
                return !triangles.empty();
            }
            catch (...)
            {
                return false;
            }
        }

        // Triangulate using GTE's exact-arithmetic Constrained Delaunay.
        static bool TriangulateWithCDT(
            std::vector<Vector2<Real>> const& points2D,
            std::vector<std::array<int32_t, 3>>& triangles)
        {
            try
            {
                auto tree = std::make_shared<PolygonTree>();
                tree->polygon.resize(points2D.size());
                std::iota(tree->polygon.begin(), tree->polygon.end(), 0);

                // Use the current (non-deprecated) single-template-parameter API.
                TriangulateCDT<Real> triangulator;
                PolygonTreeEx outputTree;
                triangulator(points2D, tree, outputTree);

                // insideTriangles = triangles inside the polygon boundary only.
                // allTriangles would also include exterior Delaunay triangles
                // that must not be used for hole filling.
                for (auto const& tri : outputTree.insideTriangles)
                {
                    triangles.push_back(tri);
                }
                return !triangles.empty();
            }
            catch (...)
            {
                // Fall back to ear clipping on CDT failure.
                return TriangulateWithEC(points2D, triangles);
            }
        }

        // ---------------------------------------------------------------------------
        // 3-D triangulation (no projection)

        // Triangulate a CW hole boundary directly in 3D via greedy ear cutting.
        //
        // For CW input the loop formula {before, after, ear_tip} produces CCW
        // (outward-facing) triangles matching the surrounding mesh.  The final
        // 3-vertex case is handled with explicit winding reversal {v0, v2, v1}.
        static bool TriangulateHole3D(
            std::vector<Vector3<Real>> const& vertices,
            HoleBoundary const& hole,
            std::vector<std::array<int32_t, 3>>& triangles)
        {
            if (hole.vertices.size() < 3)
            {
                return false;
            }

            if (hole.vertices.size() == 3)
            {
                // Reverse winding of the single triangle to get CCW output.
                triangles.push_back(
                {
                    hole.vertices[0],
                    hole.vertices[2],   // reversed
                    hole.vertices[1]    // reversed
                });
                return true;
            }

            // Each EdgeTriple stores three consecutive boundary vertices.
            struct EdgeTriple
            {
                int32_t v0, v1, v2;
            };

            size_t n = hole.vertices.size();
            std::vector<EdgeTriple> workingHole;
            workingHole.reserve(n);
            for (size_t i = 0; i < n; ++i)
            {
                workingHole.push_back(
                {
                    hole.vertices[i],
                    hole.vertices[(i + 1) % n],
                    hole.vertices[(i + 2) % n]
                });
            }

            while (workingHole.size() > 3)
            {
                int32_t bestIdx = -1;
                Real bestScore = -std::numeric_limits<Real>::max();

                for (size_t i = 0; i < workingHole.size(); ++i)
                {
                    size_t nextIdx = (i + 1) % workingHole.size();
                    Real score = ComputeEarScore3D(vertices,
                        workingHole[i], workingHole[nextIdx]);
                    if (score > bestScore)
                    {
                        bestScore = score;
                        bestIdx = static_cast<int32_t>(i);
                    }
                }

                if (bestIdx < 0)
                {
                    return false;
                }

                size_t nextIdx =
                    static_cast<size_t>(bestIdx + 1) % workingHole.size();

                EdgeTriple const& t1 = workingHole[bestIdx];
                EdgeTriple const& t2 = workingHole[nextIdx];

                // For a CW boundary: {before, after, ear_tip} is CCW (outward).
                // t1.v0 = before, t2.v1 = after, t1.v1 = ear_tip.
                triangles.push_back({t1.v0, t2.v1, t1.v1});

                // Merge the two edge triples and remove the consumed one.
                EdgeTriple merged;
                merged.v0 = t1.v0;
                merged.v1 = t2.v1;
                merged.v2 = workingHole[
                    (nextIdx + 1) % workingHole.size()].v1;

                workingHole[bestIdx] = merged;
                workingHole.erase(workingHole.begin() + nextIdx);
            }

            // Add the final 3-vertex triangle with reversed winding so that
            // it faces outward (CCW) like the rest of the fill.
            // After ear-cutting from a CW polygon the three remaining entries
            // still describe a CW triplet, so {v0, v2, v1} is CCW.
            triangles.push_back(
            {
                workingHole[0].v0,
                workingHole[2].v0,   // reversed
                workingHole[1].v0    // reversed
            });

            return true;
        }

        // Compute a quality score for a candidate ear in 3D.
        // Higher score = smoother dihedral transition at the ear tip.
        template <typename EdgeTriple>
        static Real ComputeEarScore3D(
            std::vector<Vector3<Real>> const& vertices,
            EdgeTriple const& t1,
            EdgeTriple const& t2)
        {
            Vector3<Real> const& p10 = vertices[t1.v0];
            Vector3<Real> const& p11 = vertices[t1.v1];
            Vector3<Real> const& p12 = vertices[t1.v2];
            Vector3<Real> const& p20 = vertices[t2.v0];
            Vector3<Real> const& p21 = vertices[t2.v1];
            Vector3<Real> const& p22 = vertices[t2.v2];

            Vector3<Real> n1 = Cross(p11 - p10, p12 - p10);
            Vector3<Real> n2 = Cross(p21 - p20, p22 - p20);
            Vector3<Real> n  = n1 + n2;

            Real nlen = Length(n);
            if (nlen < static_cast<Real>(1e-10))
            {
                return -std::numeric_limits<Real>::max();
            }
            n /= nlen;

            Vector3<Real> a = p11 - p10;
            Vector3<Real> b = p21 - p20;

            Real alen = Length(a);
            Real blen = Length(b);
            if (alen < static_cast<Real>(1e-10) ||
                blen < static_cast<Real>(1e-10))
            {
                return -std::numeric_limits<Real>::max();
            }
            a /= alen;
            b /= blen;

            Vector3<Real> axb = Cross(a, b);
            return -std::atan2(Dot(n, axb), Dot(a, b));
        }

        // ---------------------------------------------------------------------------
        // Geometric helpers

        // Un-normalized Newell area vector; its sign encodes boundary orientation.
        static Vector3<Real> ComputeHoleNormalRaw(
            std::vector<Vector3<Real>> const& vertices,
            HoleBoundary const& hole)
        {
            Vector3<Real> normal{
                static_cast<Real>(0),
                static_cast<Real>(0),
                static_cast<Real>(0)};

            size_t n = hole.vertices.size();
            for (size_t i = 0; i < n; ++i)
            {
                Vector3<Real> const& curr = vertices[hole.vertices[i]];
                Vector3<Real> const& next =
                    vertices[hole.vertices[(i + 1) % n]];

                normal[0] += (curr[1] - next[1]) * (curr[2] + next[2]);
                normal[1] += (curr[2] - next[2]) * (curr[0] + next[0]);
                normal[2] += (curr[0] - next[0]) * (curr[1] + next[1]);
            }
            return normal;   // NOT normalized — sign is meaningful
        }

        // Normalized Newell normal used for the projection plane.
        // For a CW boundary this points inward; the resulting projection is
        // always CCW (positive 2D area) which is required by TriangulateEC.
        static Vector3<Real> ComputeHoleNormal(
            std::vector<Vector3<Real>> const& vertices,
            HoleBoundary const& hole)
        {
            Vector3<Real> n = ComputeHoleNormalRaw(vertices, hole);
            Normalize(n);
            return n;
        }

        // Average area-weighted normal of all mesh triangles.
        // Used to classify boundary loops as CW (inner hole) or CCW (outer edge).
        static Vector3<Real> ComputeAverageMeshNormal(
            std::vector<Vector3<Real>> const& vertices,
            std::vector<std::array<int32_t, 3>> const& triangles)
        {
            Vector3<Real> avg{
                static_cast<Real>(0),
                static_cast<Real>(0),
                static_cast<Real>(0)};

            for (auto const& tri : triangles)
            {
                Vector3<Real> e1 = vertices[tri[1]] - vertices[tri[0]];
                Vector3<Real> e2 = vertices[tri[2]] - vertices[tri[0]];
                avg += Cross(e1, e2);   // area-weighted contribution
            }

            Real len = Length(avg);
            if (len > static_cast<Real>(1e-10))
            {
                avg /= len;
            }
            return avg;
        }

        // Build a tangent-space basis {uAxis, vAxis} orthogonal to 'normal'.
        static void ComputeTangentSpace(
            Vector3<Real> const& normal,
            Vector3<Real>& uAxis,
            Vector3<Real>& vAxis)
        {
            Vector3<Real> v[3];
            v[0] = normal;
            ComputeOrthogonalComplement(1, v, true);
            uAxis = v[1];
            vAxis = v[2];
        }

        // Maximum deviation of hole boundary vertices from the best-fit plane.
        static Real ComputeHolePlanarity(
            std::vector<Vector3<Real>> const& vertices,
            HoleBoundary const& hole,
            Vector3<Real> const& normal)
        {
            if (hole.vertices.size() < 3)
            {
                return static_cast<Real>(0);
            }

            Vector3<Real> centroid{
                static_cast<Real>(0),
                static_cast<Real>(0),
                static_cast<Real>(0)};
            for (int32_t idx : hole.vertices)
            {
                centroid += vertices[idx];
            }
            centroid /= static_cast<Real>(hole.vertices.size());

            Real maxDev = static_cast<Real>(0);
            for (int32_t idx : hole.vertices)
            {
                Real dev = std::abs(Dot(vertices[idx] - centroid, normal));
                if (dev > maxDev)
                {
                    maxDev = dev;
                }
            }
            return maxDev;
        }

        // Hole size estimate (perimeter).
        static Real EstimateHoleSize(
            std::vector<Vector3<Real>> const& vertices,
            HoleBoundary const& hole)
        {
            Real perimeter = static_cast<Real>(0);
            size_t n = hole.vertices.size();
            for (size_t i = 0; i < n; ++i)
            {
                perimeter += Length(
                    vertices[hole.vertices[(i + 1) % n]] -
                    vertices[hole.vertices[i]]);
            }
            return perimeter;
        }

        // Total surface area of a set of triangles.
        static Real ComputeTriangulationArea(
            std::vector<Vector3<Real>> const& vertices,
            std::vector<std::array<int32_t, 3>> const& tris)
        {
            Real area = static_cast<Real>(0);
            for (auto const& tri : tris)
            {
                Vector3<Real> e1 = vertices[tri[1]] - vertices[tri[0]];
                Vector3<Real> e2 = vertices[tri[2]] - vertices[tri[0]];
                area += Length(Cross(e1, e2)) * static_cast<Real>(0.5);
            }
            return area;
        }
    };
}
