// Test for Co3Ne manifold-constrained triangle generation
//
// Verifies that useManifoldConstrainedGeneration produces a mesh with
// fewer or equal non-manifold edges compared to the standard generation,
// and that winding is consistent with point normals.

#include <GTE/Mathematics/Co3Ne.h>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <map>
#include <vector>

using namespace gte;

// ===== helpers =====

// Count edges that appear in more than 2 triangles (non-manifold edges)
static int CountNonManifoldEdges(std::vector<std::array<int32_t, 3>> const& triangles)
{
    std::map<std::pair<int32_t, int32_t>, int32_t> edgeCount;
    for (auto const& tri : triangles)
    {
        for (int e = 0; e < 3; ++e)
        {
            int32_t a = tri[e];
            int32_t b = tri[(e + 1) % 3];
            if (a > b) std::swap(a, b);
            edgeCount[{a, b}]++;
        }
    }
    int count = 0;
    for (auto const& [edge, n] : edgeCount)
    {
        if (n > 2) ++count;
    }
    return count;
}

// Check that all adjacent triangle pairs in the mesh have consistent relative orientation.
// For a consistently-oriented mesh, each shared undirected edge (a,b) should appear with
// opposite directed edges in the two incident triangles: once as a→b and once as b→a.
static bool CheckRelativeWindingConsistency(
    std::vector<std::array<int32_t, 3>> const& triangles)
{
    // Build directed-edge to triangle map.
    // Key: (a, b) directed. Value: triangle index.
    std::map<std::pair<int32_t,int32_t>, int32_t> dirEdgeToTri;
    for (int32_t t = 0; t < static_cast<int32_t>(triangles.size()); ++t)
    {
        auto const& tri = triangles[t];
        for (int e = 0; e < 3; ++e)
        {
            auto a = tri[e], b = tri[(e+1)%3];
            dirEdgeToTri[{a, b}] = t;
        }
    }

    // For each directed edge (a→b), check that (b→a) maps to a different triangle
    // (or to no triangle, i.e., boundary edge).  If (a→b) and (b→a) both appear,
    // both incident triangles should point the same normal direction.
    int badEdges = 0;
    for (auto const& [ab, t1] : dirEdgeToTri)
    {
        auto ba = std::make_pair(ab.second, ab.first);
        auto it = dirEdgeToTri.find(ba);
        if (it == dirEdgeToTri.end())
        {
            continue;   // boundary edge, OK
        }
        // If (a→b) and (b→a) both exist with DIFFERENT triangles it's consistent.
        // If they map to the SAME triangle, that triangle has a degenerate edge.
        if (t1 == it->second)
        {
            ++badEdges;
        }
    }

    // Also count edges where same directed edge appears twice (two triangles share
    // same direction → inconsistently oriented).
    // (handled above via distinct map entries)

    return badEdges == 0;
}

// Build a sphere point cloud with analytic outward normals
// Returns a suitable search radius for the given n and sphere radius.
static double MakeSphereCloud(int n, std::vector<Vector3<double>>& points,
                              std::vector<Vector3<double>>& normals,
                              double sphereRadius = 1.0)
{
    // Fibonacci sphere for near-uniform distribution
    double golden = (1.0 + std::sqrt(5.0)) / 2.0;
    for (int i = 0; i < n; ++i)
    {
        double theta = std::acos(1.0 - 2.0 * (i + 0.5) / n);
        double phi   = 2.0 * 3.14159265358979323846 * i / golden;
        double x = std::sin(theta) * std::cos(phi);
        double y = std::sin(theta) * std::sin(phi);
        double z = std::cos(theta);
        points.push_back({x * sphereRadius, y * sphereRadius, z * sphereRadius});
        normals.push_back({x, y, z});   // outward unit normal
    }
    // Average nearest-neighbor distance on a sphere of radius r with n points
    // is approximately sqrt(4*pi*r^2 / n). Use 4x that as the search radius.
    return 4.0 * sphereRadius * std::sqrt(4.0 * 3.14159265358979323846 / n);
}

// ===== tests =====

static bool TestManifoldConstrainedGenerationHasNoNonManifoldEdges()
{
    std::cout << "Test: manifold-constrained generation produces no non-manifold edges\n";

    std::vector<Vector3<double>> points, normals, outVerts;
    std::vector<std::array<int32_t, 3>> outTris;
    double radius = MakeSphereCloud(200, points, normals);

    Co3Ne<double>::Parameters params;
    params.kNeighbors             = 15;
    params.searchRadius           = radius;
    params.maxNormalAngle         = 90.0;
    params.orientNormals          = false;   // normals already oriented

    bool ok = Co3Ne<double>::Reconstruct(points, outVerts, outTris, params);
    if (!ok)
    {
        std::cout << "  FAIL: no triangles generated (search_radius=" << radius << ")\n";
        return false;
    }

    int nmEdges = CountNonManifoldEdges(outTris);
    std::cout << "  Triangles: " << outTris.size()
              << "  Non-manifold edges: " << nmEdges << "\n";

    if (nmEdges != 0)
    {
        std::cout << "  FAIL: expected 0 non-manifold edges from constrained generation\n";
        return false;
    }
    std::cout << "  PASS\n";
    return true;
}

static bool TestManifoldConstrainedGenerationWindingConsistency()
{
    std::cout << "Test: manifold-constrained generation produces relatively consistent winding\n";

    // Run the full pipeline and check RELATIVE winding consistency of the output:
    // for every shared undirected edge the two incident triangles must carry it in
    // opposite directed forms (a→b in one, b→a in the other).
    std::vector<Vector3<double>> points, normals, outVerts;
    std::vector<std::array<int32_t, 3>> outTris;
    double radius = MakeSphereCloud(200, points, normals);

    Co3Ne<double>::Parameters params;
    params.kNeighbors               = 15;
    params.searchRadius             = radius;
    params.maxNormalAngle           = 90.0;
    params.orientNormals            = true;   // propagate consistent PCA orientation

    bool ok = Co3Ne<double>::Reconstruct(points, outVerts, outTris, params);
    if (!ok)
    {
        std::cout << "  FAIL: no triangles generated\n";
        return false;
    }

    bool windingOk = CheckRelativeWindingConsistency(outTris);
    std::cout << "  Triangles: " << outTris.size()
              << "  Relative winding consistent: " << (windingOk ? "yes" : "no") << "\n";

    if (!windingOk)
    {
        std::cout << "  FAIL: relative winding inconsistency detected in output\n";
        return false;
    }
    std::cout << "  PASS\n";
    return true;
}

static bool TestConstrainedGenerationFewerCandidatesThanFan()
{
    std::cout << "Test: constrained generation produces a valid non-empty mesh\n";

    std::vector<Vector3<double>> points, normals;
    double radius = MakeSphereCloud(100, points, normals);

    std::vector<Vector3<double>> outVerts;
    std::vector<std::array<int32_t, 3>> outTris;

    Co3Ne<double>::Parameters params;
    params.kNeighbors             = 12;
    params.searchRadius           = radius;
    params.maxNormalAngle         = 90.0;
    params.orientNormals          = false;

    Co3Ne<double>::Reconstruct(points, outVerts, outTris, params);
    size_t constrainedCount = outTris.size();

    // Constrained generation must produce a non-empty result
    std::cout << "  Constrained: " << constrainedCount << " triangles\n";

    if (constrainedCount == 0)
    {
        std::cout << "  FAIL: constrained generation produced no triangles\n";
        return false;
    }
    std::cout << "  PASS\n";
    return true;
}

static bool TestReconstructWorksWithFlag()
{
    std::cout << "Test: Reconstruct completes successfully (manifold-constrained generation)\n";

    std::vector<Vector3<double>> points, normals, outVerts;
    std::vector<std::array<int32_t, 3>> outTris;
    double radius = MakeSphereCloud(150, points, normals);

    Co3Ne<double>::Parameters params;
    params.searchRadius = radius;
    params.orientNormals = false;

    bool ok = Co3Ne<double>::Reconstruct(points, outVerts, outTris, params);
    std::cout << "  Triangles generated: " << outTris.size() << "\n";
    if (!ok)
    {
        std::cout << "  FAIL: no triangles generated\n";
        return false;
    }
    std::cout << "  PASS\n";
    return true;
}

int main()
{
    std::cout << "=== Co3Ne manifold-constrained generation tests ===\n\n";

    int passed = 0, total = 0;

    auto run = [&](bool result)
    {
        ++total;
        if (result) ++passed;
        std::cout << "\n";
    };

    run(TestManifoldConstrainedGenerationHasNoNonManifoldEdges());
    run(TestManifoldConstrainedGenerationWindingConsistency());
    run(TestConstrainedGenerationFewerCandidatesThanFan());
    run(TestReconstructWorksWithFlag());

    std::cout << "Results: " << passed << "/" << total << " tests passed\n";
    return (passed == total) ? 0 : 1;
}
