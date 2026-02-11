// David Eberly, Geometric Tools, Redmond WA 98052
// Copyright (c) 1998-2026
// Distributed under the Boost Software License, Version 1.0.
// https://www.boost.org/LICENSE_1_0.txt
// https://www.geometrictools.com/License/Boost/LICENSE_1_0.txt
// File Version: 8.0.2026.02.11
//
// CVT Newton Optimizer using BFGS (Broyden-Fletcher-Goldfarb-Shanno)
//
// Based on Geogram's CVT optimizer in CVT.cpp:
// - Newton iterations with BFGS Hessian approximation
// - Line search with Armijo backtracking
// - CVT functional minimization
//
// The CVT functional to minimize is:
//   F(sites) = sum over cells { integral of |x - site|^2 dx }
// 
// Gradient:
//   dF/d(site) = 2 * mass * (site - centroid)
//
// Adapted for Geometric Tools Engine:
// - Uses GTE's Vector3 and numerical types
// - Integrates with RestrictedVoronoiDiagram
// - Uses IntegrationSimplex for gradient computation

#pragma once

#include <GTE/Mathematics/Vector3.h>
#include <GTE/Mathematics/RestrictedVoronoiDiagram.h>
#include <GTE/Mathematics/IntegrationSimplex.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <vector>

namespace gte
{
    template <typename Real>
    class CVTOptimizer
    {
    public:
        struct Parameters
        {
            size_t maxNewtonIterations;     // Maximum Newton iterations
            Real gradientTolerance;         // Convergence tolerance for gradient norm
            Real functionalTolerance;       // Convergence tolerance for functional change
            Real lineSearchAlpha;           // Initial step size for line search
            Real lineSearchBeta;            // Backtracking factor (0 < beta < 1)
            Real lineSearchC;               // Armijo condition parameter (0 < c < 1)
            size_t maxLineSearchSteps;      // Maximum backtracking steps
            bool verbose;                   // Print optimization progress

            Parameters()
                : maxNewtonIterations(10)
                , gradientTolerance(static_cast<Real>(1e-6))
                , functionalTolerance(static_cast<Real>(1e-8))
                , lineSearchAlpha(static_cast<Real>(1.0))
                , lineSearchBeta(static_cast<Real>(0.5))
                , lineSearchC(static_cast<Real>(0.1))
                , maxLineSearchSteps(20)
                , verbose(false)
            {
            }
        };

        struct OptimizationResult
        {
            bool converged;                 // Did optimization converge?
            size_t iterations;              // Number of iterations performed
            Real finalFunctional;           // Final CVT functional value
            Real finalGradientNorm;         // Final gradient norm
            std::vector<Real> functionalHistory;  // Functional value at each iteration
            std::vector<Real> gradientNormHistory; // Gradient norm at each iteration

            OptimizationResult()
                : converged(false)
                , iterations(0)
                , finalFunctional(std::numeric_limits<Real>::max())
                , finalGradientNorm(std::numeric_limits<Real>::max())
            {
            }
        };

        // Main optimization function
        // Optimizes Voronoi sites to minimize CVT functional
        // mesh: Surface mesh (vertices and triangles)
        // sites: Input/output Voronoi sites (optimized in place)
        static OptimizationResult Optimize(
            std::vector<Vector3<Real>> const& meshVertices,
            std::vector<std::array<int32_t, 3>> const& meshTriangles,
            std::vector<Vector3<Real>>& sites,
            Parameters const& params = Parameters())
        {
            OptimizationResult result;

            if (sites.empty())
            {
                return result;
            }

            // Initialize RVD
            RestrictedVoronoiDiagram<Real> rvd;

            // BFGS variables
            size_t n = sites.size() * 3;  // 3D sites, flattened
            std::vector<Real> x = FlattenSites(sites);
            std::vector<Real> gradient(n);
            std::vector<Real> direction(n);
            
            // Initial functional and gradient
            Real functional = ComputeFunctionalAndGradient(
                meshVertices, meshTriangles, sites, gradient, rvd);
            
            result.functionalHistory.push_back(functional);
            Real gradNorm = ComputeNorm(gradient);
            result.gradientNormHistory.push_back(gradNorm);

            if (params.verbose)
            {
                std::cout << "CVT Newton Optimization\n";
                std::cout << "Iteration 0: F = " << functional 
                          << ", ||grad|| = " << gradNorm << "\n";
            }

            // Check initial convergence
            if (gradNorm < params.gradientTolerance)
            {
                result.converged = true;
                result.finalFunctional = functional;
                result.finalGradientNorm = gradNorm;
                return result;
            }

            // BFGS iteration
            std::vector<std::vector<Real>> H;  // Hessian approximation (inverse)
            InitializeIdentityMatrix(H, n);    // Start with identity

            for (size_t iter = 0; iter < params.maxNewtonIterations; ++iter)
            {
                // Compute search direction: d = -H * gradient
                MatrixVectorMultiply(H, gradient, direction);
                for (size_t i = 0; i < n; ++i)
                {
                    direction[i] = -direction[i];
                }

                // Line search to find step size
                Real alpha = LineSearch(
                    meshVertices, meshTriangles, sites, x, direction, 
                    functional, gradient, rvd, params);

                if (alpha == static_cast<Real>(0))
                {
                    // Line search failed
                    if (params.verbose)
                    {
                        std::cout << "Line search failed, stopping\n";
                    }
                    break;
                }

                // Update sites: x_new = x + alpha * direction
                std::vector<Real> x_new(n);
                for (size_t i = 0; i < n; ++i)
                {
                    x_new[i] = x[i] + alpha * direction[i];
                }

                // Unflatten to sites
                std::vector<Vector3<Real>> sites_new = UnflattenSites(x_new);

                // Compute new functional and gradient
                std::vector<Real> gradient_new(n);
                Real functional_new = ComputeFunctionalAndGradient(
                    meshVertices, meshTriangles, sites_new, gradient_new, rvd);

                // BFGS update of Hessian approximation
                std::vector<Real> s(n);  // x_new - x
                std::vector<Real> y(n);  // gradient_new - gradient
                
                for (size_t i = 0; i < n; ++i)
                {
                    s[i] = x_new[i] - x[i];
                    y[i] = gradient_new[i] - gradient[i];
                }

                BFGSUpdate(H, s, y);

                // Update state
                x = x_new;
                sites = sites_new;
                functional = functional_new;
                gradient = gradient_new;

                Real gradNorm = ComputeNorm(gradient);
                result.functionalHistory.push_back(functional);
                result.gradientNormHistory.push_back(gradNorm);
                result.iterations = iter + 1;

                if (params.verbose)
                {
                    std::cout << "Iteration " << (iter + 1) 
                              << ": F = " << functional 
                              << ", ||grad|| = " << gradNorm 
                              << ", alpha = " << alpha << "\n";
                }

                // Check convergence
                if (gradNorm < params.gradientTolerance)
                {
                    result.converged = true;
                    result.finalFunctional = functional;
                    result.finalGradientNorm = gradNorm;
                    if (params.verbose)
                    {
                        std::cout << "Converged: gradient norm below tolerance\n";
                    }
                    break;
                }

                if (iter > 0)
                {
                    Real functionalChange = std::abs(
                        result.functionalHistory[iter] - result.functionalHistory[iter - 1]);
                    if (functionalChange < params.functionalTolerance * std::abs(functional))
                    {
                        result.converged = true;
                        result.finalFunctional = functional;
                        result.finalGradientNorm = gradNorm;
                        if (params.verbose)
                        {
                            std::cout << "Converged: functional change below tolerance\n";
                        }
                        break;
                    }
                }
            }

            if (!result.converged)
            {
                result.finalFunctional = functional;
                result.finalGradientNorm = ComputeNorm(gradient);
                if (params.verbose)
                {
                    std::cout << "Maximum iterations reached\n";
                }
            }

            return result;
        }

    private:
        // Flatten sites to vector
        static std::vector<Real> FlattenSites(std::vector<Vector3<Real>> const& sites)
        {
            std::vector<Real> x(sites.size() * 3);
            for (size_t i = 0; i < sites.size(); ++i)
            {
                x[i * 3 + 0] = sites[i][0];
                x[i * 3 + 1] = sites[i][1];
                x[i * 3 + 2] = sites[i][2];
            }
            return x;
        }

        // Unflatten vector to sites
        static std::vector<Vector3<Real>> UnflattenSites(std::vector<Real> const& x)
        {
            std::vector<Vector3<Real>> sites(x.size() / 3);
            for (size_t i = 0; i < sites.size(); ++i)
            {
                sites[i][0] = x[i * 3 + 0];
                sites[i][1] = x[i * 3 + 1];
                sites[i][2] = x[i * 3 + 2];
            }
            return sites;
        }

        // Compute CVT functional and gradient
        static Real ComputeFunctionalAndGradient(
            std::vector<Vector3<Real>> const& meshVertices,
            std::vector<std::array<int32_t, 3>> const& meshTriangles,
            std::vector<Vector3<Real>> const& sites,
            std::vector<Real>& gradient,
            RestrictedVoronoiDiagram<Real>& rvd)
        {
            // Initialize RVD
            if (!rvd.Initialize(meshVertices, meshTriangles, sites))
            {
                return std::numeric_limits<Real>::max();
            }

            // Compute cells
            std::vector<typename RestrictedVoronoiDiagram<Real>::RVD_Cell> cells;
            if (!rvd.ComputeCells(cells))
            {
                return std::numeric_limits<Real>::max();
            }

            // Compute functional and gradient for each site
            Real functional = static_cast<Real>(0);
            std::fill(gradient.begin(), gradient.end(), static_cast<Real>(0));

            for (size_t i = 0; i < cells.size(); ++i)
            {
                auto const& cell = cells[i];
                Vector3<Real> const& site = sites[i];

                // Collect polygons
                std::vector<std::vector<Vector3<Real>>> polygons;
                for (auto const& poly : cell.polygons)
                {
                    polygons.push_back(poly.vertices);
                }

                // Compute cell properties with functional and gradient
                auto props = IntegrationSimplex<Real>::ComputeCellProperties(
                    polygons, site, true);

                functional += props.functional;

                // Gradient for site i
                gradient[i * 3 + 0] = props.gradient[0];
                gradient[i * 3 + 1] = props.gradient[1];
                gradient[i * 3 + 2] = props.gradient[2];
            }

            return functional;
        }

        // Line search with Armijo backtracking
        static Real LineSearch(
            std::vector<Vector3<Real>> const& meshVertices,
            std::vector<std::array<int32_t, 3>> const& meshTriangles,
            std::vector<Vector3<Real>> const& sites,
            std::vector<Real> const& x,
            std::vector<Real> const& direction,
            Real functional,
            std::vector<Real> const& gradient,
            RestrictedVoronoiDiagram<Real>& rvd,
            Parameters const& params)
        {
            Real alpha = params.lineSearchAlpha;
            
            // Directional derivative: gradient dot direction
            Real dirDerivative = static_cast<Real>(0);
            for (size_t i = 0; i < gradient.size(); ++i)
            {
                dirDerivative += gradient[i] * direction[i];
            }

            // Armijo condition threshold
            Real threshold = params.lineSearchC * dirDerivative;

            for (size_t step = 0; step < params.maxLineSearchSteps; ++step)
            {
                // Try x + alpha * direction
                std::vector<Real> x_trial(x.size());
                for (size_t i = 0; i < x.size(); ++i)
                {
                    x_trial[i] = x[i] + alpha * direction[i];
                }

                std::vector<Vector3<Real>> sites_trial = UnflattenSites(x_trial);
                std::vector<Real> gradient_trial(gradient.size());
                
                Real functional_trial = ComputeFunctionalAndGradient(
                    meshVertices, meshTriangles, sites_trial, gradient_trial, rvd);

                // Check Armijo condition
                if (functional_trial <= functional + alpha * threshold)
                {
                    return alpha;
                }

                // Backtrack
                alpha *= params.lineSearchBeta;
            }

            // Line search failed
            return static_cast<Real>(0);
        }

        // BFGS update of inverse Hessian approximation
        static void BFGSUpdate(
            std::vector<std::vector<Real>>& H,
            std::vector<Real> const& s,
            std::vector<Real> const& y)
        {
            size_t n = s.size();
            
            // Compute rho = 1 / (y^T s)
            Real yTs = static_cast<Real>(0);
            for (size_t i = 0; i < n; ++i)
            {
                yTs += y[i] * s[i];
            }

            if (std::abs(yTs) < std::numeric_limits<Real>::epsilon())
            {
                // Skip update if curvature condition violated
                return;
            }

            Real rho = static_cast<Real>(1) / yTs;

            // Compute Hy
            std::vector<Real> Hy(n, static_cast<Real>(0));
            for (size_t i = 0; i < n; ++i)
            {
                for (size_t j = 0; j < n; ++j)
                {
                    Hy[i] += H[i][j] * y[j];
                }
            }

            // Compute y^T H y
            Real yTHy = static_cast<Real>(0);
            for (size_t i = 0; i < n; ++i)
            {
                yTHy += y[i] * Hy[i];
            }

            // BFGS update: H = H + (rho * s * s^T) * (1 + rho * y^T H y) - rho * (s * Hy^T + Hy * s^T)
            for (size_t i = 0; i < n; ++i)
            {
                for (size_t j = 0; j < n; ++j)
                {
                    H[i][j] += rho * s[i] * s[j] * (static_cast<Real>(1) + rho * yTHy)
                             - rho * (s[i] * Hy[j] + Hy[i] * s[j]);
                }
            }
        }

        // Helper functions
        static void InitializeIdentityMatrix(std::vector<std::vector<Real>>& H, size_t n)
        {
            H.resize(n);
            for (size_t i = 0; i < n; ++i)
            {
                H[i].resize(n, static_cast<Real>(0));
                H[i][i] = static_cast<Real>(1);
            }
        }

        static void MatrixVectorMultiply(
            std::vector<std::vector<Real>> const& H,
            std::vector<Real> const& v,
            std::vector<Real>& result)
        {
            size_t n = v.size();
            result.resize(n);
            for (size_t i = 0; i < n; ++i)
            {
                result[i] = static_cast<Real>(0);
                for (size_t j = 0; j < n; ++j)
                {
                    result[i] += H[i][j] * v[j];
                }
            }
        }

        static Real ComputeNorm(std::vector<Real> const& v)
        {
            Real sum = static_cast<Real>(0);
            for (auto val : v)
            {
                sum += val * val;
            }
            return std::sqrt(sum);
        }
    };
}
