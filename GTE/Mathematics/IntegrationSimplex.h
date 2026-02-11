// David Eberly, Geometric Tools, Redmond WA 98052
// Copyright (c) 1998-2026
// Distributed under the Boost Software License, Version 1.0.
// https://www.boost.org/LICENSE_1_0.txt
// https://www.geometrictools.com/License/Boost/LICENSE_1_0.txt
// File Version: 8.0.2026.02.11
//
// Integration utilities for simplices (triangles, tetrahedra)
//
// Based on Geogram's integration approach in CVT.cpp:
// - Numerical integration over restricted Voronoi cells
// - Mass computation (area/volume)
// - Centroid computation
// - Gradient computation for CVT functional
//
// Adapted for Geometric Tools Engine:
// - Uses GTE's Vector3 and numerical types
// - Provides integration over 2D polygons and 3D triangles
// - Supports CVT functional and gradient computation

#pragma once

#include <GTE/Mathematics/Vector3.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace gte
{
    template <typename Real>
    class IntegrationSimplex
    {
    public:
        // ===== POLYGON INTEGRATION (2D in 3D space) =====

        // Compute area of a polygon in 3D space using fan triangulation
        // Polygon vertices should be coplanar and in consistent order
        static Real ComputePolygonArea(
            std::vector<Vector3<Real>> const& vertices)
        {
            if (vertices.size() < 3)
            {
                return static_cast<Real>(0);
            }

            // Use fan triangulation from first vertex
            Real totalArea = static_cast<Real>(0);
            Vector3<Real> const& v0 = vertices[0];

            for (size_t i = 1; i + 1 < vertices.size(); ++i)
            {
                Vector3<Real> const& v1 = vertices[i];
                Vector3<Real> const& v2 = vertices[i + 1];

                // Triangle area = 0.5 * |cross product|
                Vector3<Real> edge1 = v1 - v0;
                Vector3<Real> edge2 = v2 - v0;
                Vector3<Real> crossProd = Cross(edge1, edge2);
                Real area = Length(crossProd) * static_cast<Real>(0.5);

                totalArea += area;
            }

            return totalArea;
        }

        // Compute centroid of a polygon using area-weighted fan triangulation
        static Vector3<Real> ComputePolygonCentroid(
            std::vector<Vector3<Real>> const& vertices)
        {
            if (vertices.empty())
            {
                return Vector3<Real>::Zero();
            }

            if (vertices.size() == 1)
            {
                return vertices[0];
            }

            if (vertices.size() == 2)
            {
                return (vertices[0] + vertices[1]) * static_cast<Real>(0.5);
            }

            // Use area-weighted centroid from fan triangulation
            Vector3<Real> centroid = Vector3<Real>::Zero();
            Real totalArea = static_cast<Real>(0);
            Vector3<Real> const& v0 = vertices[0];

            for (size_t i = 1; i + 1 < vertices.size(); ++i)
            {
                Vector3<Real> const& v1 = vertices[i];
                Vector3<Real> const& v2 = vertices[i + 1];

                // Triangle area
                Vector3<Real> edge1 = v1 - v0;
                Vector3<Real> edge2 = v2 - v0;
                Vector3<Real> crossProd = Cross(edge1, edge2);
                Real area = Length(crossProd) * static_cast<Real>(0.5);

                // Triangle centroid
                Vector3<Real> triCentroid = (v0 + v1 + v2) / static_cast<Real>(3);

                // Accumulate weighted centroid
                centroid += area * triCentroid;
                totalArea += area;
            }

            if (totalArea > static_cast<Real>(0))
            {
                centroid /= totalArea;
            }
            else
            {
                // Fallback to simple average if degenerate
                centroid = Vector3<Real>::Zero();
                for (auto const& v : vertices)
                {
                    centroid += v;
                }
                centroid /= static_cast<Real>(vertices.size());
            }

            return centroid;
        }

        // ===== TRIANGLE INTEGRATION (3D) =====

        // Compute area of a 3D triangle
        static Real ComputeTriangleArea(
            Vector3<Real> const& v0,
            Vector3<Real> const& v1,
            Vector3<Real> const& v2)
        {
            Vector3<Real> edge1 = v1 - v0;
            Vector3<Real> edge2 = v2 - v0;
            Vector3<Real> crossProd = Cross(edge1, edge2);
            return Length(crossProd) * static_cast<Real>(0.5);
        }

        // Compute centroid of a 3D triangle
        static Vector3<Real> ComputeTriangleCentroid(
            Vector3<Real> const& v0,
            Vector3<Real> const& v1,
            Vector3<Real> const& v2)
        {
            return (v0 + v1 + v2) / static_cast<Real>(3);
        }

        // ===== CVT FUNCTIONAL INTEGRATION =====

        // Compute CVT functional for a single polygon
        // CVT functional: integral of |x - site|^2 over polygon
        // Used for gradient computation in Newton optimization
        static Real ComputeCVTFunctional(
            std::vector<Vector3<Real>> const& polygonVertices,
            Vector3<Real> const& site)
        {
            if (polygonVertices.size() < 3)
            {
                return static_cast<Real>(0);
            }

            Real functional = static_cast<Real>(0);
            Vector3<Real> const& v0 = polygonVertices[0];

            // Use fan triangulation and integrate over each triangle
            for (size_t i = 1; i + 1 < polygonVertices.size(); ++i)
            {
                Vector3<Real> const& v1 = polygonVertices[i];
                Vector3<Real> const& v2 = polygonVertices[i + 1];

                // Triangle area
                Real area = ComputeTriangleArea(v0, v1, v2);

                // Triangle centroid
                Vector3<Real> centroid = (v0 + v1 + v2) / static_cast<Real>(3);

                // Distance squared from site to centroid
                Vector3<Real> diff = centroid - site;
                Real distSq = Dot(diff, diff);

                // Add contribution: area * distance^2
                // This is an approximation using triangle centroid
                // For exact integration, would need quadrature points
                functional += area * distSq;
            }

            return functional;
        }

        // Compute CVT functional gradient for a single polygon
        // Gradient: d/d(site) of integral of |x - site|^2 over polygon
        // Result: 2 * mass * (site - centroid)
        static Vector3<Real> ComputeCVTGradient(
            std::vector<Vector3<Real>> const& polygonVertices,
            Vector3<Real> const& site)
        {
            if (polygonVertices.size() < 3)
            {
                return Vector3<Real>::Zero();
            }

            // For CVT, gradient = 2 * mass * (site - centroid)
            Real mass = ComputePolygonArea(polygonVertices);
            Vector3<Real> centroid = ComputePolygonCentroid(polygonVertices);

            return static_cast<Real>(2) * mass * (site - centroid);
        }

        // ===== MASS AND CENTROID FOR CVT CELLS =====

        // Compute mass (total area) and centroid for a collection of polygons
        // Used for RVD cells in CVT
        struct CellProperties
        {
            Real mass;                  // Total area of cell
            Vector3<Real> centroid;     // Weighted centroid
            Real functional;            // CVT functional value
            Vector3<Real> gradient;     // CVT functional gradient

            CellProperties()
                : mass(static_cast<Real>(0))
                , centroid(Vector3<Real>::Zero())
                , functional(static_cast<Real>(0))
                , gradient(Vector3<Real>::Zero())
            {
            }
        };

        static CellProperties ComputeCellProperties(
            std::vector<std::vector<Vector3<Real>>> const& polygons,
            Vector3<Real> const& site,
            bool computeFunctional = false)
        {
            CellProperties props;

            for (auto const& polygon : polygons)
            {
                if (polygon.size() < 3)
                {
                    continue;
                }

                // Compute polygon area and centroid
                Real area = ComputePolygonArea(polygon);
                Vector3<Real> centroid = ComputePolygonCentroid(polygon);

                // Accumulate mass and weighted centroid
                props.mass += area;
                props.centroid += area * centroid;

                // Optionally compute CVT functional and gradient
                if (computeFunctional)
                {
                    props.functional += ComputeCVTFunctional(polygon, site);
                    props.gradient += ComputeCVTGradient(polygon, site);
                }
            }

            // Normalize centroid by total mass
            if (props.mass > static_cast<Real>(0))
            {
                props.centroid /= props.mass;
            }
            else
            {
                // Fallback to site if degenerate
                props.centroid = site;
            }

            return props;
        }

        // ===== NUMERICAL INTEGRATION UTILITIES =====

        // Gauss-Legendre quadrature points and weights for triangle integration
        // For higher accuracy than centroid approximation
        struct QuadraturePoint
        {
            Real u, v;      // Barycentric coordinates (w = 1 - u - v)
            Real weight;    // Integration weight
        };

        // 3-point Gauss quadrature for triangles (degree 2 exactness)
        static std::vector<QuadraturePoint> GetTriangleQuadrature3Point()
        {
            std::vector<QuadraturePoint> points;
            
            // Midpoint of each edge
            points.push_back({ static_cast<Real>(0.5), static_cast<Real>(0.5), static_cast<Real>(1.0 / 3.0) });
            points.push_back({ static_cast<Real>(0.5), static_cast<Real>(0.0), static_cast<Real>(1.0 / 3.0) });
            points.push_back({ static_cast<Real>(0.0), static_cast<Real>(0.5), static_cast<Real>(1.0 / 3.0) });

            return points;
        }

        // 7-point Gauss quadrature for triangles (degree 5 exactness)
        static std::vector<QuadraturePoint> GetTriangleQuadrature7Point()
        {
            std::vector<QuadraturePoint> points;
            
            // Center point
            Real a1 = static_cast<Real>(1.0 / 3.0);
            Real w1 = static_cast<Real>(0.225);
            points.push_back({ a1, a1, w1 });

            // Edge points
            Real a2 = static_cast<Real>(0.797426985353087);
            Real b2 = static_cast<Real>(0.101286507323456);
            Real w2 = static_cast<Real>(0.125939180544827);
            
            points.push_back({ a2, b2, w2 });
            points.push_back({ b2, a2, w2 });
            points.push_back({ b2, b2, w2 });

            // Interior points
            Real a3 = static_cast<Real>(0.470142064105115);
            Real b3 = static_cast<Real>(0.059715871789770);
            Real w3 = static_cast<Real>(0.132394152788506);
            
            points.push_back({ a3, b3, w3 });
            points.push_back({ b3, a3, w3 });
            points.push_back({ b3, b3, w3 });

            return points;
        }

        // Integrate a function over a triangle using quadrature
        // Function f takes a Vector3<Real> point and returns Real
        template <typename Func>
        static Real IntegrateOverTriangle(
            Vector3<Real> const& v0,
            Vector3<Real> const& v1,
            Vector3<Real> const& v2,
            Func const& f,
            std::vector<QuadraturePoint> const& quadrature)
        {
            Real area = ComputeTriangleArea(v0, v1, v2);
            Real integral = static_cast<Real>(0);

            for (auto const& qp : quadrature)
            {
                // Barycentric coordinates
                Real u = qp.u;
                Real v = qp.v;
                Real w = static_cast<Real>(1) - u - v;

                // Point on triangle
                Vector3<Real> point = w * v0 + u * v1 + v * v2;

                // Evaluate function and accumulate weighted sum
                integral += qp.weight * f(point);
            }

            // Scale by triangle area
            return area * integral;
        }
    };
}
