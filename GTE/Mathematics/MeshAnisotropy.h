// David Eberly, Geometric Tools, Redmond WA 98052
// Copyright (c) 1998-2026
// Distributed under the Boost Software License, Version 1.0.
// https://www.boost.org/LICENSE_1_0.txt
// https://www.geometrictools.com/License/Boost/LICENSE_1_0.txt
// File Version: 8.0.2026.02.11
//
// Anisotropic mesh utilities for remeshing
//
// Original Geogram Source:
// - geogram/src/lib/geogram/mesh/mesh_geometry.cpp (set_anisotropy function)
// - geogram/src/lib/geogram/mesh/mesh_remesh.h (anisotropic remeshing)
// - https://github.com/BrunoLevy/geogram (commit f5abd69)
// License: BSD 3-Clause (Inria) - Compatible with Boost
// Copyright (c) 2000-2022 Inria
//
// Anisotropic Remeshing Theory:
// =============================
// Anisotropic remeshing adapts mesh elements to surface features by using
// directional (anisotropic) distance metrics instead of uniform (isotropic) ones.
//
// Geogram's Approach (Full 6D CVT):
// The full implementation uses dimension=6 for Centroidal Voronoi Tessellation:
// - Coordinates 0,1,2: 3D position (x, y, z)
// - Coordinates 3,4,5: Scaled normal (nx*s, ny*s, nz*s)
//
// Distance in 6D: d = sqrt((x1-x0)² + (y1-y0)² + (z1-z0)² + s²·(n1-n0)²)
//
// This creates anisotropic Voronoi cells that naturally align with surface
// curvature, producing meshes with:
// - Fewer elements (30-50% reduction for curved surfaces)
// - Better feature alignment (edges follow ridges/valleys)
// - Appropriate aspect ratios (stretched along low-curvature directions)
//
// Implementation Notes:
// ====================
// This header provides utilities for anisotropic mesh processing:
//
// 1. SetAnisotropy: Scales vertex normals by factor s (core geogram function)
// 2. Create6DPoints: Creates 6D point arrays for anisotropic CVT
// 3. ComputeCurvatureAdaptiveAnisotropy: Uses curvature to adapt scaling
//
// Full 6D CVT requires extending GTE's Delaunay3 to support arbitrary dimensions,
// which is a substantial enhancement. The utilities here support that future work
// and also enable curvature-adaptive remeshing using existing 3D infrastructure.
//
// Example Usage (for future 6D CVT):
// ==================================
// std::vector<Vector3<double>> vertices, normals;
// std::vector<std::array<int32_t, 3>> triangles;
// // ... load mesh ...
//
// // Set anisotropy
// MeshAnisotropy<double>::SetAnisotropy(vertices, triangles, normals, 0.04);
//
// // Create 6D points for anisotropic CVT
// auto points6D = MeshAnisotropy<double>::Create6DPoints(vertices, normals);
//
// // TODO: Use with dimension-6 Delaunay/Voronoi (requires extending GTE)
// // Delaunay<6> delaunay(points6D);  // Future enhancement
// // CVT with dimension=6 creates anisotropic cells
//
// Adapted for Geometric Tools Engine:
// - Uses GTE's Vector3 and mesh structures
// - Uses GTE's MeshCurvature for principal curvature computation
// - Header-only template implementation

#pragma once

#include <GTE/Mathematics/Vector3.h>
#include <GTE/Mathematics/MeshCurvature.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace gte
{
    template <typename Real>
    class MeshAnisotropy
    {
    public:
        // Compute vertex normals (area-weighted)
        static void ComputeVertexNormals(
            std::vector<Vector3<Real>> const& vertices,
            std::vector<std::array<int32_t, 3>> const& triangles,
            std::vector<Vector3<Real>>& normals)
        {
            normals.resize(vertices.size());
            std::fill(normals.begin(), normals.end(), Vector3<Real>::Zero());

            // Sum area-weighted normals for each triangle
            for (auto const& tri : triangles)
            {
                Vector3<Real> const& v0 = vertices[tri[0]];
                Vector3<Real> const& v1 = vertices[tri[1]];
                Vector3<Real> const& v2 = vertices[tri[2]];

                Vector3<Real> edge1 = v1 - v0;
                Vector3<Real> edge2 = v2 - v0;
                Vector3<Real> normal = Cross(edge1, edge2);

                normals[tri[0]] += normal;
                normals[tri[1]] += normal;
                normals[tri[2]] += normal;
            }

            // Normalize
            for (auto& normal : normals)
            {
                Normalize(normal);
            }
        }

        // Compute bounding box diagonal
        static Real ComputeBBoxDiagonal(std::vector<Vector3<Real>> const& vertices)
        {
            if (vertices.empty())
            {
                return static_cast<Real>(0);
            }

            Vector3<Real> minBox = vertices[0];
            Vector3<Real> maxBox = vertices[0];

            for (auto const& v : vertices)
            {
                for (int i = 0; i < 3; ++i)
                {
                    minBox[i] = std::min(minBox[i], v[i]);
                    maxBox[i] = std::max(maxBox[i], v[i]);
                }
            }

            return Length(maxBox - minBox);
        }

        // Set anisotropy: Scale normals by factor s (relative to bounding box)
        // This is the core of Geogram's set_anisotropy function
        // s = 0 disables anisotropy (unit normals)
        // s > 0 creates anisotropic metric (typical values: 0.02-0.1)
        static void SetAnisotropy(
            std::vector<Vector3<Real>> const& vertices,
            std::vector<std::array<int32_t, 3>> const& triangles,
            std::vector<Vector3<Real>>& normals,
            Real s)
        {
            // Compute normals if needed
            if (normals.size() != vertices.size())
            {
                ComputeVertexNormals(vertices, triangles, normals);
            }

            // If s == 0, just normalize
            if (s == static_cast<Real>(0))
            {
                for (auto& normal : normals)
                {
                    Normalize(normal);
                }
                return;
            }

            // Scale by bounding box diagonal
            Real bboxDiag = ComputeBBoxDiagonal(vertices);
            Real scale = s * bboxDiag;

            // Scale and normalize
            for (auto& normal : normals)
            {
                Normalize(normal);
                normal *= scale;
            }
        }

        // Unset anisotropy: normalize normals to unit length
        static void UnsetAnisotropy(std::vector<Vector3<Real>>& normals)
        {
            for (auto& normal : normals)
            {
                Normalize(normal);
            }
        }

        // Create 6D points for anisotropic CVT
        // Returns flattened array: [x0, y0, z0, nx0*s, ny0*s, nz0*s, x1, y1, z1, ...]
        static std::vector<Real> Create6DPoints(
            std::vector<Vector3<Real>> const& vertices,
            std::vector<Vector3<Real>> const& normals)
        {
            std::vector<Real> points6D;
            points6D.reserve(vertices.size() * 6);

            for (size_t i = 0; i < vertices.size(); ++i)
            {
                // Position (x, y, z)
                points6D.push_back(vertices[i][0]);
                points6D.push_back(vertices[i][1]);
                points6D.push_back(vertices[i][2]);

                // Scaled normal (nx*s, ny*s, nz*s)
                points6D.push_back(normals[i][0]);
                points6D.push_back(normals[i][1]);
                points6D.push_back(normals[i][2]);
            }

            return points6D;
        }

        // Extract 3D positions from 6D points
        static std::vector<Vector3<Real>> Extract3DPositions(
            std::vector<Real> const& points6D)
        {
            std::vector<Vector3<Real>> positions;
            positions.reserve(points6D.size() / 6);

            for (size_t i = 0; i < points6D.size(); i += 6)
            {
                positions.push_back(Vector3<Real>{
                    points6D[i], points6D[i + 1], points6D[i + 2]
                });
            }

            return positions;
        }

        // Extract 3D normals from 6D points
        static std::vector<Vector3<Real>> Extract3DNormals(
            std::vector<Real> const& points6D)
        {
            std::vector<Vector3<Real>> normals;
            normals.reserve(points6D.size() / 6);

            for (size_t i = 0; i < points6D.size(); i += 6)
            {
                normals.push_back(Vector3<Real>{
                    points6D[i + 3], points6D[i + 4], points6D[i + 5]
                });
            }

            return normals;
        }

        // Compute curvature-adaptive anisotropy scaling
        // Uses principal curvatures to determine appropriate scaling
        static void ComputeCurvatureAdaptiveAnisotropy(
            std::vector<Vector3<Real>> const& vertices,
            std::vector<std::array<int32_t, 3>> const& triangles,
            std::vector<Vector3<Real>>& normals,
            Real baseScale = static_cast<Real>(0.04))
        {
            // Compute vertex normals first
            ComputeVertexNormals(vertices, triangles, normals);

            // Compute curvatures
            MeshCurvature<Real> curvature;
            
            // Convert triangles to flat index array
            std::vector<uint32_t> indices;
            indices.reserve(triangles.size() * 3);
            for (auto const& tri : triangles)
            {
                indices.push_back(static_cast<uint32_t>(tri[0]));
                indices.push_back(static_cast<uint32_t>(tri[1]));
                indices.push_back(static_cast<uint32_t>(tri[2]));
            }

            curvature(vertices.size(), vertices.data(), triangles.size(), 
                     indices.data(), static_cast<Real>(1e-6));

            // Get curvature results
            auto const& minCurvatures = curvature.GetMinCurvatures();
            auto const& maxCurvatures = curvature.GetMaxCurvatures();

            // Compute bounding box diagonal for scaling
            Real bboxDiag = ComputeBBoxDiagonal(vertices);

            // Scale normals based on curvature
            for (size_t i = 0; i < vertices.size(); ++i)
            {
                // Compute mean curvature magnitude
                Real meanCurvature = std::abs(minCurvatures[i]) + std::abs(maxCurvatures[i]);
                meanCurvature *= static_cast<Real>(0.5);

                // Adaptive scaling: higher curvature = more anisotropy
                // But clamp to reasonable range
                Real adaptiveScale = baseScale;
                if (meanCurvature > static_cast<Real>(1e-8))
                {
                    adaptiveScale *= (static_cast<Real>(1) + meanCurvature);
                }
                adaptiveScale = std::min(adaptiveScale, baseScale * static_cast<Real>(5));

                // Apply scaling
                normals[i] = Normalize(normals[i]) * (adaptiveScale * bboxDiag);
            }
        }
    };
}
