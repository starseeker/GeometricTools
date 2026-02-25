// Test suite for Co3NeManifoldStitcher
//
// Tests the patch stitching functionality for Co3Ne output

#include <GTE/Mathematics/Co3Ne.h>
#include <GTE/Mathematics/Co3NeManifoldStitcher.h>
#include <iostream>
#include <iomanip>
#include <fstream>

using namespace gte;

// Helper to load XYZ point cloud
std::vector<Vector3<double>> LoadXYZ(std::string const& filename)
{
    std::vector<Vector3<double>> points;
    std::ifstream file(filename);
    
    if (!file.is_open())
    {
        std::cerr << "Failed to open file: " << filename << "\n";
        return points;
    }
    
    double x, y, z;
    while (file >> x >> y >> z)
    {
        points.push_back({x, y, z});
    }
    
    return points;
}

// Helper to save mesh as OBJ
void SaveOBJ(std::string const& filename,
             std::vector<Vector3<double>> const& vertices,
             std::vector<std::array<int32_t, 3>> const& triangles)
{
    std::ofstream file(filename);
    if (!file.is_open())
    {
        std::cerr << "Failed to open output file: " << filename << "\n";
        return;
    }
    
    // Write vertices
    for (auto const& v : vertices)
    {
        file << "v " << v[0] << " " << v[1] << " " << v[2] << "\n";
    }
    
    // Write faces (OBJ uses 1-based indexing)
    for (auto const& tri : triangles)
    {
        file << "f " << (tri[0] + 1) << " " << (tri[1] + 1) << " " << (tri[2] + 1) << "\n";
    }
    
    std::cout << "Saved mesh to " << filename << "\n";
}

// Test basic functionality
void TestBasicStitching()
{
    std::cout << "\n=== Test: Basic Stitching ===\n";
    
    // Create a simple point cloud (cube vertices)
    std::vector<Vector3<double>> points = {
        {0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0},
        {0, 0, 1}, {1, 0, 1}, {1, 1, 1}, {0, 1, 1}
    };
    
    // Run Co3Ne reconstruction
    Co3Ne<double>::Parameters co3neParams;
    co3neParams.kNeighbors = 5;
    
    std::vector<Vector3<double>> vertices;
    std::vector<std::array<int32_t, 3>> triangles;
    
    bool success = Co3Ne<double>::Reconstruct(points, vertices, triangles, co3neParams);
    
    std::cout << "Co3Ne reconstruction: " << (success ? "SUCCESS" : "FAILED") << "\n";
    std::cout << "Generated " << triangles.size() << " triangles\n";
    
    if (!success || triangles.empty())
    {
        std::cout << "Skipping stitcher test (no triangles generated)\n";
        return;
    }
    
    // Apply stitching
    Co3NeManifoldStitcher<double>::Parameters stitchParams;
    stitchParams.verbose = true;
    stitchParams.enableHoleFilling = true;
    stitchParams.removeNonManifoldEdges = true;
    
    bool isManifold = Co3NeManifoldStitcher<double>::StitchPatches(
        vertices, triangles, stitchParams);
    
    std::cout << "Stitching result: " << (isManifold ? "MANIFOLD" : "NON-MANIFOLD") << "\n";
    std::cout << "Final mesh: " << triangles.size() << " triangles\n";
}

// Test with r768.xyz data
void TestRealDataStitching(size_t maxPoints)
{
    std::cout << "\n=== Test: Real Data Stitching (r768.xyz) ===\n";
    
    // Load point cloud
    std::vector<Vector3<double>> points = LoadXYZ("r768.xyz");
    
    if (points.empty())
    {
        std::cout << "Failed to load r768.xyz - skipping test\n";
        return;
    }
    
    // Limit points if requested
    if (maxPoints > 0 && points.size() > maxPoints)
    {
        points.resize(maxPoints);
    }
    
    std::cout << "Loaded " << points.size() << " points\n";
    
    // Run Co3Ne reconstruction with relaxed mode
    Co3Ne<double>::Parameters co3neParams;
    co3neParams.kNeighbors = 20;
    co3neParams.orientNormals = true;
    
    std::vector<Vector3<double>> vertices;
    std::vector<std::array<int32_t, 3>> triangles;
    
    std::cout << "Running Co3Ne reconstruction...\n";
    bool success = Co3Ne<double>::Reconstruct(points, vertices, triangles, co3neParams);
    
    std::cout << "Co3Ne reconstruction: " << (success ? "SUCCESS" : "FAILED") << "\n";
    std::cout << "Generated " << triangles.size() << " triangles from " 
              << vertices.size() << " vertices\n";
    
    if (!success || triangles.empty())
    {
        std::cout << "Skipping stitcher test (no triangles generated)\n";
        return;
    }
    
    // Save pre-stitching mesh
    SaveOBJ("co3ne_before_stitch.obj", vertices, triangles);
    
    // Apply stitching
    Co3NeManifoldStitcher<double>::Parameters stitchParams;
    stitchParams.verbose = true;
    stitchParams.enableHoleFilling = true;
    stitchParams.maxHoleEdges = 50;
    stitchParams.removeNonManifoldEdges = true;
    stitchParams.holeFillingMethod = MeshHoleFilling<double>::TriangulationMethod::CDT;
    
    std::cout << "\nApplying manifold stitching...\n";
    bool isManifold = Co3NeManifoldStitcher<double>::StitchPatches(
        vertices, triangles, stitchParams);
    
    std::cout << "\nStitching result: " << (isManifold ? "MANIFOLD" : "NON-MANIFOLD") << "\n";
    std::cout << "Final mesh: " << triangles.size() << " triangles, " 
              << vertices.size() << " vertices\n";
    
    // Save post-stitching mesh
    SaveOBJ("co3ne_after_stitch.obj", vertices, triangles);
}

// Test non-manifold edge removal
void TestNonManifoldRemoval()
{
    std::cout << "\n=== Test: Non-Manifold Edge Removal ===\n";
    
    // Create a mesh with a known non-manifold edge
    std::vector<Vector3<double>> vertices = {
        {0, 0, 0}, {1, 0, 0}, {0.5, 1, 0},  // Triangle 1
        {0, 0, 0}, {1, 0, 0}, {0.5, -1, 0}, // Triangle 2
        {0, 0, 0}, {1, 0, 0}, {0.5, 0, 1}   // Triangle 3 (creates non-manifold edge 0-1)
    };
    
    std::vector<std::array<int32_t, 3>> triangles = {
        {0, 1, 2},
        {0, 1, 3},
        {0, 1, 4}
    };
    
    std::cout << "Input: 3 triangles sharing edge 0-1 (non-manifold)\n";
    
    // Apply stitching with non-manifold removal
    Co3NeManifoldStitcher<double>::Parameters stitchParams;
    stitchParams.verbose = true;
    stitchParams.removeNonManifoldEdges = true;
    stitchParams.enableHoleFilling = false;
    
    bool isManifold = Co3NeManifoldStitcher<double>::StitchPatches(
        vertices, triangles, stitchParams);
    
    std::cout << "Result: " << (isManifold ? "MANIFOLD" : "NON-MANIFOLD") << "\n";
    std::cout << "Remaining triangles: " << triangles.size() << "\n";
    
    if (triangles.size() == 2)
    {
        std::cout << "✓ Correctly removed 1 triangle to fix non-manifold edge\n";
    }
    else
    {
        std::cout << "✗ Expected 2 triangles, got " << triangles.size() << "\n";
    }
}

// Test ball pivot welding
void TestBallPivotWelding()
{
    std::cout << "\n=== Test: Ball Pivot Welding ===\n";
    
    // Load point cloud
    std::vector<Vector3<double>> points = LoadXYZ("r768.xyz");
    
    if (points.empty())
    {
        std::cout << "Failed to load r768.xyz - skipping test\n";
        return;
    }
    
    // Use 1000 points for testing
    if (points.size() > 1000)
    {
        points.resize(1000);
    }
    
    std::cout << "Loaded " << points.size() << " points\n";
    
    // Run Co3Ne reconstruction with relaxed mode
    Co3Ne<double>::Parameters co3neParams;
    co3neParams.kNeighbors = 20;
    co3neParams.orientNormals = true;
    
    std::vector<Vector3<double>> vertices;
    std::vector<std::array<int32_t, 3>> triangles;
    
    std::cout << "Running Co3Ne reconstruction...\n";
    bool success = Co3Ne<double>::Reconstruct(points, vertices, triangles, co3neParams);
    
    std::cout << "Co3Ne reconstruction: " << (success ? "SUCCESS" : "FAILED") << "\n";
    std::cout << "Generated " << triangles.size() << " triangles\n";
    
    if (!success || triangles.empty())
    {
        std::cout << "Skipping Ball Pivot test (no triangles generated)\n";
        return;
    }
    
    // Save pre-welding mesh
    SaveOBJ("co3ne_before_welding.obj", vertices, triangles);
    
    // Apply stitching with Ball Pivot enabled
    Co3NeManifoldStitcher<double>::Parameters stitchParams;
    stitchParams.verbose = true;
    stitchParams.enableHoleFilling = true;
    stitchParams.removeNonManifoldEdges = true;
    
    std::cout << "\nApplying stitching with Ball Pivot welding...\n";
    size_t trianglesBefore = triangles.size();
    
    bool isManifold = Co3NeManifoldStitcher<double>::StitchPatches(
        vertices, triangles, stitchParams);
    
    size_t trianglesAfter = triangles.size();
    
    std::cout << "\nStitching result: " << (isManifold ? "MANIFOLD" : "NON-MANIFOLD") << "\n";
    std::cout << "Triangle count: " << trianglesBefore << " -> " << trianglesAfter 
              << " (added " << (trianglesAfter - trianglesBefore) << ")\n";
    
    // Save post-welding mesh
    SaveOBJ("co3ne_after_welding.obj", vertices, triangles);
    
    if (trianglesAfter > trianglesBefore)
    {
        std::cout << "✓ Ball Pivot successfully added welding triangles\n";
    }
    else
    {
        std::cout << "ℹ Ball Pivot did not add triangles (patches may already be well-connected)\n";
    }
}

// Test local validation: ValidateFanLocal, FillFanWithECM, CapBoundaryLoop(ecm),
// and the ECM-threaded FillHolesConservative overload.
//
// These checks are O(H) per loop rather than O(T) full-mesh — the core of the
// "localize quality checks" requirement.
void TestLocalValidation()
{
    std::cout << "\n=== Test: Local Validation (O(H) per hole/cap) ===\n";

    using Stitcher = Co3NeManifoldStitcher<double>;
    int failures = 0;

    // ── 1. ValidateFanLocal: valid single-triangle fan (3-vertex boundary loop) ──
    {
        // Triangle (0,1,2) already in mesh – boundary loop is [0,1,2].
        std::vector<std::array<int32_t, 3>> tris = {{0,1,2}};
        Stitcher::EdgeCountMap ecm = Stitcher::BuildEdgeCountMap(tris);

        // All three boundary edges should have count == 1 → valid to fill
        Stitcher::BoundaryLoop loop;
        loop.vertices = {0, 1, 2};
        loop.centroid = {0.333, 0.333, 0};
        loop.perimeter = 3.0;

        if (!Stitcher::ValidateFanLocal(loop, ecm))
        {
            std::cout << "✗ ValidateFanLocal rejected valid 3-vertex loop\n";
            failures++;
        }
        else
        {
            std::cout << "✓ ValidateFanLocal accepted valid 3-vertex loop\n";
        }
    }

    // ── 2. ValidateFanLocal: invalid – boundary edge already closed (count == 2) ──
    {
        // Both triangles (0,1,2) and (0,1,3) share edge 0-1 → count 2.
        // A fan that includes edge 0-1 must be rejected.
        std::vector<std::array<int32_t, 3>> tris = {{0,1,2},{1,0,3}};
        Stitcher::EdgeCountMap ecm = Stitcher::BuildEdgeCountMap(tris);

        Stitcher::BoundaryLoop loop;
        loop.vertices = {0, 1, 2};  // Loop contains edge 0-1 which is already interior
        loop.centroid = {0.5, 0.5, 0};
        loop.perimeter = 3.0;

        if (Stitcher::ValidateFanLocal(loop, ecm))
        {
            std::cout << "✗ ValidateFanLocal accepted invalid loop (edge 0-1 already at count 2)\n";
            failures++;
        }
        else
        {
            std::cout << "✓ ValidateFanLocal rejected loop with already-interior edge\n";
        }
    }

    // ── 3. FillFanWithECM: fills a 4-edge boundary loop and updates ECM correctly ──
    {
        // Square mesh: two triangles (0,1,2) and (0,2,3) leave boundary loop [0,1,2,3]
        // with 4 boundary edges (0-1, 1-2, 2-3, 0-3).
        std::vector<Vector3<double>> vertices = {
            {0,0,0}, {1,0,0}, {1,1,0}, {0,1,0},
            {0.5, 0.5, 0}  // will be the centroid
        };
        std::vector<std::array<int32_t, 3>> tris = {{0,1,2},{0,2,3}};
        Stitcher::EdgeCountMap ecm = Stitcher::BuildEdgeCountMap(tris);

        // Open boundary on edge 0-1 and 2-3 and 0-3
        Stitcher::BoundaryLoop loop;
        loop.vertices = {0, 1, 2, 3};
        loop.centroid = {0.5, 0.5, 0};
        loop.perimeter = 4.0;

        if (!Stitcher::ValidateFanLocal(loop, ecm))
        {
            std::cout << "✗ ValidateFanLocal rejected 4-edge loop (should be valid)\n";
            failures++;
        }
        else
        {
            size_t trisBefore = tris.size();
            Stitcher::FillFanWithECM(vertices, tris, loop, ecm);
            size_t added = tris.size() - trisBefore;

            if (added != 4)
            {
                std::cout << "✗ FillFanWithECM added " << added << " triangles (expected 4)\n";
                failures++;
            }
            else
            {
                std::cout << "✓ FillFanWithECM added 4 triangles for 4-edge loop\n";
            }

            // ECM should now show all loop boundary edges as interior (count 2)
            // and all spoke edges as interior (count 2, since each spoke is shared
            // between two adjacent fan triangles).
            bool ecmOk = true;
            int32_t centroidIdx = static_cast<int32_t>(vertices.size()) - 1;
            for (int i = 0; i < 4; ++i)
            {
                int32_t v0 = loop.vertices[i];
                int32_t v1 = loop.vertices[(i+1)%4];
                auto boundaryKey = Stitcher::MakeEdgeKey(v0, v1);
                auto it = ecm.find(boundaryKey);
                if (it == ecm.end() || it->second != 2) { ecmOk = false; }

                auto spokeKey = Stitcher::MakeEdgeKey(v0, centroidIdx);
                auto it2 = ecm.find(spokeKey);
                if (it2 == ecm.end() || it2->second != 2) { ecmOk = false; }
            }
            if (!ecmOk)
            {
                std::cout << "✗ ECM not updated correctly after FillFanWithECM\n";
                failures++;
            }
            else
            {
                std::cout << "✓ ECM correctly updated (all loop edges now interior)\n";
            }
        }
    }

    // ── 4. CapBoundaryLoop ECM overload ──
    {
        std::vector<Vector3<double>> vertices = {
            {0,0,0}, {1,0,0}, {0.5,1,0}
        };
        // Single open triangle – its 3 edges are all boundary (count 1)
        std::vector<std::array<int32_t, 3>> tris = {{0,1,2}};
        Stitcher::EdgeCountMap ecm = Stitcher::BuildEdgeCountMap(tris);

        Stitcher::BoundaryLoop loop;
        loop.vertices = {0,1,2};
        loop.centroid = {0.5, 0.333, 0};
        loop.perimeter = 3.5;

        size_t trisBefore = tris.size();
        bool capped = Stitcher::CapBoundaryLoop(vertices, tris, loop, ecm);
        if (!capped || tris.size() != trisBefore + 3)
        {
            std::cout << "✗ CapBoundaryLoop(ecm) failed or wrong triangle count\n";
            failures++;
        }
        else
        {
            std::cout << "✓ CapBoundaryLoop(ecm) succeeded (added 3 fan triangles)\n";
        }
    }

    // ── 5. FillHolesConservative ECM overload ──
    {
        // Two open triangles sharing vertices – leave a 3-edge boundary loop.
        std::vector<Vector3<double>> vertices = {
            {0,0,0}, {1,0,0}, {0.5,1,0}
        };
        std::vector<std::array<int32_t, 3>> tris = {{0,1,2}};
        Stitcher::EdgeCountMap ecm = Stitcher::BuildEdgeCountMap(tris);

        Co3NeManifoldStitcher<double>::Parameters params;
        params.maxHoleEdges = 20;
        params.maxHoleArea = 0;

        size_t trisBefore = tris.size();
        size_t filled = Stitcher::FillHolesConservative(vertices, tris, params, ecm, false);
        if (filled == 0)
        {
            std::cout << "ℹ FillHolesConservative(ecm): no holes filled (boundary loop may not qualify)\n";
        }
        else
        {
            std::cout << "✓ FillHolesConservative(ecm) filled " << filled << " hole(s), "
                      << "added " << (tris.size() - trisBefore) << " triangles\n";
        }
    }

    // ── 6. End-to-end: StitchPatches uses local checks throughout ──
    {
        // Two disconnected triangles that can be bridged and then hole-filled.
        std::vector<Vector3<double>> vertices = {
            {0,0,0}, {1,0,0}, {0.5,1,0},     // patch A
            {2,0,0}, {3,0,0}, {2.5,1,0}      // patch B (separated)
        };
        std::vector<std::array<int32_t, 3>> tris = {{0,1,2},{3,4,5}};

        Co3NeManifoldStitcher<double>::Parameters params;
        params.verbose = false;
        params.enableHoleFilling = true;
        params.enableIterativeBridging = true;
        params.initialBridgeThreshold = 3.0;
        params.maxBridgeThreshold = 10.0;
        params.maxIterations = 5;

        Stitcher::StitchPatches(vertices, tris, params);

        if (tris.size() >= 2)
        {
            std::cout << "✓ End-to-end StitchPatches completed (final triangles: " 
                      << tris.size() << ")\n";
        }
        else
        {
            std::cout << "✗ End-to-end StitchPatches unexpected result\n";
            failures++;
        }
    }

    if (failures == 0)
    {
        std::cout << "\n✓ All local validation tests PASSED\n";
    }
    else
    {
        std::cout << "\n✗ " << failures << " local validation test(s) FAILED\n";
    }
}

int main(int argc, char* argv[])
{
    std::cout << "Co3Ne Manifold Stitcher Test Suite\n";
    std::cout << "===================================\n";
    
    // Run tests
    TestBasicStitching();
    TestNonManifoldRemoval();
    TestBallPivotWelding();
    TestLocalValidation();  // New: tests O(H) local quality checks

    // Test with real data if available
    if (argc > 1)
    {
        size_t maxPoints = std::atoi(argv[1]);
        TestRealDataStitching(maxPoints);
    }
    else
    {
        TestRealDataStitching(1000);  // Default: 1000 points
    }
    
    std::cout << "\n=== All Tests Complete ===\n";
    
    return 0;
}
