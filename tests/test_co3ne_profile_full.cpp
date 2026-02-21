// Co3Ne Full-Pipeline Profiling and Manifold Validation Test
//
// Profiles the Co3Ne meshing pipeline across multiple point-count tiers
// using r768.xyz as the baseline data source, then verifies that the
// final output mesh is a single, closed, manifold solid.
//
// Pass/fail criteria:
//   - Reconstruction succeeds and produces triangles
//   - Output is a single connected component
//   - Zero boundary edges  (closed surface)
//   - Zero non-manifold edges
//   - Euler characteristic V - E + F == 2  (sphere topology)

#include <GTE/Mathematics/Co3Ne.h>
#include <GTE/Mathematics/Co3NeManifoldStitcher.h>
#include <GTE/Mathematics/Vector3.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

using namespace gte;
using Clock = std::chrono::high_resolution_clock;

// ---------------------------------------------------------------------------
// Load XYZ (with or without per-point normals)
// ---------------------------------------------------------------------------
static bool LoadXYZ(std::string const& filename,
                    std::vector<Vector3<double>>& points)
{
    std::ifstream in(filename);
    if (!in.is_open())
    {
        std::cerr << "Cannot open: " << filename << "\n";
        return false;
    }
    points.clear();
    std::string line;
    while (std::getline(in, line))
    {
        if (line.empty()) continue;
        std::istringstream iss(line);
        double x, y, z;
        if (!(iss >> x >> y >> z)) continue;
        points.push_back(Vector3<double>{x, y, z});
        // skip optional nx ny nz
    }
    return !points.empty();
}

// ---------------------------------------------------------------------------
// Detailed topology analysis
// ---------------------------------------------------------------------------
struct TopologyResult
{
    size_t vertices;
    size_t edges;
    size_t triangles;
    size_t boundaryEdges;
    size_t nonManifoldEdges;
    size_t components;
    int    eulerCharacteristic;   // V - E + F
    bool   isClosedManifold;      // boundary==0 && nonManifold==0 && components==1
    bool   isSingleComponent;
};

static TopologyResult AnalyzeTopology(
    std::vector<Vector3<double>> const& vertices,
    std::vector<std::array<int32_t, 3>> const& triangles)
{
    TopologyResult r{};
    r.triangles = triangles.size();

    if (triangles.empty())
    {
        return r;
    }

    // --- edge counts ---
    struct PairHash
    {
        size_t operator()(std::pair<int32_t, int32_t> const& p) const noexcept
        {
            size_t h1 = std::hash<int32_t>{}(p.first);
            size_t h2 = std::hash<int32_t>{}(p.second);
            return h1 ^ (h2 * 2654435761ULL);
        }
    };

    std::unordered_map<std::pair<int32_t, int32_t>,
                       std::vector<int32_t>, PairHash> edgeToTris;
    edgeToTris.reserve(triangles.size() * 3);

    std::set<int32_t> usedVerts;
    for (int32_t ti = 0; ti < static_cast<int32_t>(triangles.size()); ++ti)
    {
        auto const& t = triangles[ti];
        for (int i = 0; i < 3; ++i)
        {
            usedVerts.insert(t[i]);
            int32_t a = t[i], b = t[(i + 1) % 3];
            if (a > b) std::swap(a, b);
            edgeToTris[{a, b}].push_back(ti);
        }
    }

    r.vertices = usedVerts.size();
    r.edges    = edgeToTris.size();

    for (auto const& kv : edgeToTris)
    {
        size_t cnt = kv.second.size();
        if (cnt == 1) ++r.boundaryEdges;
        else if (cnt > 2) ++r.nonManifoldEdges;
    }

    // --- connected components (BFS over triangle adjacency) ---
    std::vector<bool> visited(triangles.size(), false);
    r.components = 0;
    for (size_t seed = 0; seed < triangles.size(); ++seed)
    {
        if (visited[seed]) continue;
        ++r.components;
        std::vector<int32_t> stack;
        stack.push_back(static_cast<int32_t>(seed));
        visited[seed] = true;
        while (!stack.empty())
        {
            int32_t cur = stack.back();
            stack.pop_back();
            auto const& curTri = triangles[static_cast<size_t>(cur)];
            for (int i = 0; i < 3; ++i)
            {
                int32_t a = curTri[i], b = curTri[(i + 1) % 3];
                if (a > b) std::swap(a, b);
                auto it = edgeToTris.find({a, b});
                if (it == edgeToTris.end()) continue;
                for (int32_t nb : it->second)
                {
                    if (!visited[static_cast<size_t>(nb)])
                    {
                        visited[static_cast<size_t>(nb)] = true;
                        stack.push_back(nb);
                    }
                }
            }
        }
    }

    r.eulerCharacteristic =
        static_cast<int>(r.vertices) -
        static_cast<int>(r.edges) +
        static_cast<int>(r.triangles);

    r.isSingleComponent = (r.components == 1);
    r.isClosedManifold  = (r.boundaryEdges == 0) &&
                          (r.nonManifoldEdges == 0) &&
                          r.isSingleComponent;

    return r;
}

// ---------------------------------------------------------------------------
// Print a topology result
// ---------------------------------------------------------------------------
static void PrintTopology(TopologyResult const& t, bool expectClosed)
{
    std::cout << "  Vertices (used): " << t.vertices << "\n";
    std::cout << "  Edges:           " << t.edges << "\n";
    std::cout << "  Triangles:       " << t.triangles << "\n";
    std::cout << "  Boundary edges:  " << t.boundaryEdges;
    if (expectClosed && t.boundaryEdges > 0) std::cout << "  *** OPEN ***";
    std::cout << "\n";
    std::cout << "  Non-manifold:    " << t.nonManifoldEdges;
    if (t.nonManifoldEdges > 0) std::cout << "  *** NON-MANIFOLD ***";
    std::cout << "\n";
    std::cout << "  Components:      " << t.components;
    if (expectClosed && t.components != 1) std::cout << "  *** FRAGMENTED ***";
    std::cout << "\n";
    std::cout << "  Euler (V-E+F):   " << t.eulerCharacteristic;
    if (t.eulerCharacteristic == 2) std::cout << "  (sphere topology ✓)";
    std::cout << "\n";
    std::cout << "  Closed manifold: " << (t.isClosedManifold ? "YES ✓" : "NO ✗") << "\n";
}

// ---------------------------------------------------------------------------
// Run one profiling tier
// ---------------------------------------------------------------------------
struct TierResult
{
    size_t inputPoints;
    size_t outputVertices;
    size_t outputTriangles;
    double reconstructionSec;
    double stitchingSec;
    double totalSec;
    TopologyResult topology;
    bool reconstructionSucceeded;
};

static TierResult RunTier(std::vector<Vector3<double>> const& points,
                          bool verbose)
{
    TierResult tr{};
    tr.inputPoints = points.size();
    tr.reconstructionSucceeded = false;

    if (verbose)
        std::cout << "\n--- " << points.size() << " points ---\n";

    // Phase 1: Co3Ne reconstruction
    Co3Ne<double>::Parameters co3neParams;
    co3neParams.kNeighbors               = 20;
    co3neParams.relaxedManifoldExtraction = true;
    co3neParams.orientNormals             = true;

    std::vector<Vector3<double>>      vertices;
    std::vector<std::array<int32_t, 3>> triangles;

    auto t0 = Clock::now();
    bool ok = Co3Ne<double>::Reconstruct(points, vertices, triangles, co3neParams);
    tr.reconstructionSec = std::chrono::duration<double>(Clock::now() - t0).count();

    if (!ok || triangles.empty())
    {
        if (verbose) std::cout << "  Reconstruction FAILED\n";
        return tr;
    }
    tr.reconstructionSucceeded = true;

    if (verbose)
    {
        std::cout << "  Reconstruction: " << std::fixed << std::setprecision(2)
                  << tr.reconstructionSec << "s  ("
                  << vertices.size() << " verts, " << triangles.size() << " tris)\n";
    }

    // Phase 2: Manifold stitching
    Co3NeManifoldStitcher<double>::Parameters stitchParams;
    stitchParams.verbose                 = false;
    stitchParams.enableIterativeBridging = true;
    stitchParams.enableHoleFilling       = true;
    stitchParams.enableBallPivotHoleFiller = false;
    stitchParams.initialBridgeThreshold  = 2.0;
    stitchParams.maxBridgeThreshold      = 10.0;
    stitchParams.maxIterations           = 20;

    auto t1 = Clock::now();
    Co3NeManifoldStitcher<double>::StitchPatches(vertices, triangles, stitchParams);
    tr.stitchingSec = std::chrono::duration<double>(Clock::now() - t1).count();
    tr.totalSec     = tr.reconstructionSec + tr.stitchingSec;

    tr.outputVertices  = vertices.size();
    tr.outputTriangles = triangles.size();

    if (verbose)
    {
        std::cout << "  Stitching:      " << std::fixed << std::setprecision(2)
                  << tr.stitchingSec << "s  ("
                  << triangles.size() << " tris after stitching)\n";
    }

    // Phase 3: topology analysis
    tr.topology = AnalyzeTopology(vertices, triangles);

    if (verbose)
    {
        PrintTopology(tr.topology, true);
    }

    return tr;
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main(int argc, char* argv[])
{
    // Allow overriding the xyz path from the command line.
    std::string xyzPath = (argc > 1) ? argv[1] : "r768.xyz";

    std::cout << "============================================================\n";
    std::cout << "  Co3Ne Full-Pipeline Profiling and Manifold Validation\n";
    std::cout << "============================================================\n\n";

    // Load the full dataset
    std::vector<Vector3<double>> allPoints;
    if (!LoadXYZ(xyzPath, allPoints))
    {
        std::cerr << "Failed to load: " << xyzPath << "\n";
        return 1;
    }
    std::cout << "Dataset: " << xyzPath << "  (" << allPoints.size() << " points)\n\n";

    // Reproducible random subset generation
    std::vector<size_t> idx(allPoints.size());
    std::iota(idx.begin(), idx.end(), 0);
    std::mt19937 rng(42);
    std::shuffle(idx.begin(), idx.end(), rng);

    // Tier sizes: small -> large.  Always include the full dataset.
    std::vector<size_t> tiers = {1000, 5000, 10000};
    if (allPoints.size() > 10000)
        tiers.push_back(allPoints.size());

    std::vector<TierResult> results;
    const double timeoutSec = 120.0;

    for (size_t sz : tiers)
    {
        if (sz > allPoints.size()) continue;

        std::vector<Vector3<double>> pts(sz);
        for (size_t i = 0; i < sz; ++i)
            pts[i] = allPoints[idx[i]];

        auto tr = RunTier(pts, true);
        results.push_back(tr);

        if (tr.totalSec > timeoutSec)
        {
            std::cout << "\n[stopping: cumulative runtime exceeded "
                      << timeoutSec << "s]\n";
            break;
        }
    }

    // -----------------------------------------------------------------------
    // Summary table
    // -----------------------------------------------------------------------
    std::cout << "\n============================================================\n";
    std::cout << "  Performance Summary\n";
    std::cout << "============================================================\n\n";

    std::cout << std::setw(8)  << "Points"
              << std::setw(10) << "Verts"
              << std::setw(10) << "Tris"
              << std::setw(12) << "Reconstr."
              << std::setw(12) << "Stitch"
              << std::setw(12) << "Total"
              << std::setw(10) << "Euler"
              << std::setw(10) << "Bndry"
              << std::setw(10) << "Comps"
              << "  Closed?\n";
    std::cout << std::string(104, '-') << "\n";

    for (auto const& r : results)
    {
        if (!r.reconstructionSucceeded) continue;
        std::cout << std::setw(8)  << r.inputPoints
                  << std::setw(10) << r.outputVertices
                  << std::setw(10) << r.outputTriangles
                  << std::setw(11) << std::fixed << std::setprecision(2)
                                   << r.reconstructionSec << "s"
                  << std::setw(11) << std::fixed << std::setprecision(2)
                                   << r.stitchingSec << "s"
                  << std::setw(11) << std::fixed << std::setprecision(2)
                                   << r.totalSec << "s"
                  << std::setw(10) << r.topology.eulerCharacteristic
                  << std::setw(10) << r.topology.boundaryEdges
                  << std::setw(10) << r.topology.components
                  << "  " << (r.topology.isClosedManifold ? "YES ✓" : "NO  ✗")
                  << "\n";
    }

    // -----------------------------------------------------------------------
    // Scaling analysis
    // -----------------------------------------------------------------------
    if (results.size() >= 2)
    {
        std::cout << "\n============================================================\n";
        std::cout << "  Scaling Analysis\n";
        std::cout << "============================================================\n\n";

        for (size_t i = 1; i < results.size(); ++i)
        {
            auto const& prev = results[i - 1];
            auto const& curr = results[i];
            if (!prev.reconstructionSucceeded || !curr.reconstructionSucceeded)
                continue;

            double szRatio   = static_cast<double>(curr.inputPoints) /
                               static_cast<double>(prev.inputPoints);
            double timeRatio = curr.totalSec / std::max(prev.totalSec, 1e-9);
            double k         = std::log(timeRatio) / std::log(szRatio);

            std::cout << prev.inputPoints << " -> " << curr.inputPoints
                      << "  pts x" << std::fixed << std::setprecision(2) << szRatio
                      << "  time x" << std::fixed << std::setprecision(2) << timeRatio
                      << "  O(n^" << std::fixed << std::setprecision(2) << k << ")\n";
        }
    }

    // -----------------------------------------------------------------------
    // Full-dataset closed-manifold validation (primary pass/fail criterion)
    // -----------------------------------------------------------------------
    std::cout << "\n============================================================\n";
    std::cout << "  Full-Dataset Manifold Validation\n";
    std::cout << "============================================================\n\n";

    // Find the result for the full dataset (largest tier we actually ran)
    TierResult const* fullResult = nullptr;
    for (auto const& r : results)
    {
        if (r.inputPoints == allPoints.size() && r.reconstructionSucceeded)
        {
            fullResult = &r;
            break;
        }
    }

    if (!fullResult)
    {
        std::cerr << "Full-dataset tier did not complete successfully.\n";
        return 1;
    }

    PrintTopology(fullResult->topology, true);

    bool passed = fullResult->topology.isClosedManifold &&
                  (fullResult->topology.eulerCharacteristic == 2);

    std::cout << "\n";
    if (passed)
    {
        std::cout << "RESULT: PASS — output is a single, closed, manifold solid "
                     "(Euler = 2).\n";
    }
    else
    {
        std::cout << "RESULT: FAIL — output does NOT meet closed-manifold solid "
                     "criteria.\n";
        if (!fullResult->topology.isSingleComponent)
            std::cout << "  * Multiple components: "
                      << fullResult->topology.components << "\n";
        if (fullResult->topology.boundaryEdges > 0)
            std::cout << "  * Open boundary: "
                      << fullResult->topology.boundaryEdges << " edges\n";
        if (fullResult->topology.nonManifoldEdges > 0)
            std::cout << "  * Non-manifold: "
                      << fullResult->topology.nonManifoldEdges << " edges\n";
        if (fullResult->topology.eulerCharacteristic != 2)
            std::cout << "  * Euler characteristic: "
                      << fullResult->topology.eulerCharacteristic
                      << " (expected 2 for sphere topology)\n";
    }

    return passed ? 0 : 1;
}
