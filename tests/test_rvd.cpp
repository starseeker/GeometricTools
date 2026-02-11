// Test program for RestrictedVoronoiDiagram implementation

#include <GTE/Mathematics/RestrictedVoronoiDiagram.h>
#include <GTE/Mathematics/Vector3.h>
#include <iostream>
#include <vector>
#include <array>
#include <fstream>
#include <sstream>

using namespace gte;

// Helper: Save OBJ file
bool SaveOBJ(std::string const& filename,
             std::vector<Vector3<double>> const& vertices,
             std::vector<std::array<int32_t, 3>> const& triangles)
{
    std::ofstream file(filename);
    if (!file.is_open())
    {
        std::cerr << "Failed to create: " << filename << std::endl;
        return false;
    }

    for (auto const& v : vertices)
    {
        file << "v " << v[0] << " " << v[1] << " " << v[2] << "\n";
    }

    for (auto const& tri : triangles)
    {
        file << "f " << (tri[0] + 1) << " " << (tri[1] + 1) << " " << (tri[2] + 1) << "\n";
    }

    std::cout << "Saved " << vertices.size() << " vertices, "
              << triangles.size() << " triangles to " << filename << std::endl;
    return true;
}

// Test 1: Simple square mesh with 2 sites
void TestSimpleSquare()
{
    std::cout << "\n===== Test 1: Simple Square Mesh =====" << std::endl;

    // Create a simple square mesh (2 triangles)
    std::vector<Vector3<double>> meshVertices = {
        {0.0, 0.0, 0.0},  // 0
        {1.0, 0.0, 0.0},  // 1
        {1.0, 1.0, 0.0},  // 2
        {0.0, 1.0, 0.0}   // 3
    };

    std::vector<std::array<int32_t, 3>> meshTriangles = {
        {0, 1, 2},  // Lower triangle
        {0, 2, 3}   // Upper triangle
    };

    // Two Voronoi sites
    std::vector<Vector3<double>> voronoiSites = {
        {0.25, 0.5, 0.0},  // Left site
        {0.75, 0.5, 0.0}   // Right site
    };

    // Create RVD
    RestrictedVoronoiDiagram<double> rvd;
    RestrictedVoronoiDiagram<double>::Parameters params;
    params.computeIntegration = true;

    if (!rvd.Initialize(meshVertices, meshTriangles, voronoiSites, params))
    {
        std::cout << "FAILED: RVD initialization" << std::endl;
        return;
    }

    // Compute cells
    std::vector<RestrictedVoronoiDiagram<double>::RVD_Cell> cells;
    if (!rvd.ComputeCells(cells))
    {
        std::cout << "FAILED: Computing RVD cells" << std::endl;
        return;
    }

    std::cout << "SUCCESS: Computed " << cells.size() << " Voronoi cells" << std::endl;

    // Display results
    for (size_t i = 0; i < cells.size(); ++i)
    {
        auto const& cell = cells[i];
        std::cout << "  Cell " << i << ":" << std::endl;
        std::cout << "    Site: (" << voronoiSites[i][0] << ", " << voronoiSites[i][1] << ", " << voronoiSites[i][2] << ")" << std::endl;
        std::cout << "    Centroid: (" << cell.centroid[0] << ", " << cell.centroid[1] << ", " << cell.centroid[2] << ")" << std::endl;
        std::cout << "    Mass (area): " << cell.mass << std::endl;
        std::cout << "    Polygons: " << cell.polygons.size() << std::endl;
    }

    // Test centroid computation
    std::vector<Vector3<double>> centroids;
    if (rvd.ComputeCentroids(centroids))
    {
        std::cout << "\nCentroids computed successfully:" << std::endl;
        for (size_t i = 0; i < centroids.size(); ++i)
        {
            std::cout << "  Site " << i << ": (" << centroids[i][0] << ", " << centroids[i][1] << ", " << centroids[i][2] << ")" << std::endl;
        }
    }
}

// Test 2: Cube mesh with corner sites
void TestCube()
{
    std::cout << "\n===== Test 2: Cube Mesh =====" << std::endl;

    // Create a simple cube mesh
    std::vector<Vector3<double>> meshVertices = {
        // Bottom face
        {0.0, 0.0, 0.0}, {1.0, 0.0, 0.0}, {1.0, 1.0, 0.0}, {0.0, 1.0, 0.0},
        // Top face
        {0.0, 0.0, 1.0}, {1.0, 0.0, 1.0}, {1.0, 1.0, 1.0}, {0.0, 1.0, 1.0}
    };

    std::vector<std::array<int32_t, 3>> meshTriangles = {
        // Bottom
        {0, 1, 2}, {0, 2, 3},
        // Top
        {4, 6, 5}, {4, 7, 6},
        // Front
        {0, 5, 1}, {0, 4, 5},
        // Back
        {2, 7, 3}, {2, 6, 7},
        // Left
        {0, 3, 7}, {0, 7, 4},
        // Right
        {1, 6, 2}, {1, 5, 6}
    };

    // Voronoi sites at cube corners
    std::vector<Vector3<double>> voronoiSites = {
        {0.2, 0.2, 0.2},  // Near corner 0
        {0.8, 0.8, 0.8}   // Near corner 6
    };

    // Create RVD
    RestrictedVoronoiDiagram<double> rvd;
    if (!rvd.Initialize(meshVertices, meshTriangles, voronoiSites))
    {
        std::cout << "FAILED: RVD initialization" << std::endl;
        return;
    }

    // Compute cells
    std::vector<RestrictedVoronoiDiagram<double>::RVD_Cell> cells;
    if (!rvd.ComputeCells(cells))
    {
        std::cout << "FAILED: Computing RVD cells" << std::endl;
        return;
    }

    std::cout << "SUCCESS: Computed " << cells.size() << " Voronoi cells" << std::endl;

    // Display results
    for (size_t i = 0; i < cells.size(); ++i)
    {
        auto const& cell = cells[i];
        std::cout << "  Cell " << i << ":" << std::endl;
        std::cout << "    Site: (" << voronoiSites[i][0] << ", " << voronoiSites[i][1] << ", " << voronoiSites[i][2] << ")" << std::endl;
        std::cout << "    Centroid: (" << cell.centroid[0] << ", " << cell.centroid[1] << ", " << cell.centroid[2] << ")" << std::endl;
        std::cout << "    Mass (area): " << cell.mass << std::endl;
        std::cout << "    Polygons: " << cell.polygons.size() << std::endl;
    }
}

// Test 3: Lloyd iteration with RVD
void TestLloydIteration()
{
    std::cout << "\n===== Test 3: Lloyd Iteration with RVD =====" << std::endl;

    // Simple square mesh
    std::vector<Vector3<double>> meshVertices = {
        {0.0, 0.0, 0.0}, {2.0, 0.0, 0.0}, {2.0, 2.0, 0.0}, {0.0, 2.0, 0.0}
    };

    std::vector<std::array<int32_t, 3>> meshTriangles = {
        {0, 1, 2}, {0, 2, 3}
    };

    // Initial sites (not centered)
    std::vector<Vector3<double>> sites = {
        {0.3, 0.3, 0.0},
        {1.7, 0.3, 0.0},
        {1.7, 1.7, 0.0},
        {0.3, 1.7, 0.0}
    };

    std::cout << "Initial sites:" << std::endl;
    for (size_t i = 0; i < sites.size(); ++i)
    {
        std::cout << "  Site " << i << ": (" << sites[i][0] << ", " << sites[i][1] << ", " << sites[i][2] << ")" << std::endl;
    }

    // Lloyd iterations
    RestrictedVoronoiDiagram<double> rvd;
    const int numIterations = 3;

    for (int iter = 0; iter < numIterations; ++iter)
    {
        std::cout << "\nIteration " << (iter + 1) << ":" << std::endl;

        // Initialize RVD with current sites
        if (!rvd.Initialize(meshVertices, meshTriangles, sites))
        {
            std::cout << "FAILED: RVD initialization" << std::endl;
            return;
        }

        // Compute new centroids
        std::vector<Vector3<double>> centroids;
        if (!rvd.ComputeCentroids(centroids))
        {
            std::cout << "FAILED: Computing centroids" << std::endl;
            return;
        }

        // Move sites to centroids
        double maxDisplacement = 0.0;
        for (size_t i = 0; i < sites.size(); ++i)
        {
            double displacement = Length(centroids[i] - sites[i]);
            maxDisplacement = std::max(maxDisplacement, displacement);
            sites[i] = centroids[i];
            std::cout << "  Site " << i << ": (" << sites[i][0] << ", " << sites[i][1] << ", " << sites[i][2] << ")"
                      << " (moved " << displacement << ")" << std::endl;
        }

        std::cout << "  Max displacement: " << maxDisplacement << std::endl;

        if (maxDisplacement < 1e-6)
        {
            std::cout << "  Converged!" << std::endl;
            break;
        }
    }

    std::cout << "\nFinal sites:" << std::endl;
    for (size_t i = 0; i < sites.size(); ++i)
    {
        std::cout << "  Site " << i << ": (" << sites[i][0] << ", " << sites[i][1] << ", " << sites[i][2] << ")" << std::endl;
    }
}

int main(int argc, char* argv[])
{
    std::cout << "===== RestrictedVoronoiDiagram Test Suite =====" << std::endl;

    TestSimpleSquare();
    TestCube();
    TestLloydIteration();

    std::cout << "\n===== Tests Complete =====" << std::endl;
    return 0;
}
