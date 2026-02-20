// End-to-end test: PatchClusterMerger pipeline on the sample r768_1000.xyz
// point cloud.
//
// This test exercises:
//   1. Loading a 6-column XYZ file (x y z nx ny nz).
//   2. Reconstructing with Co3Ne::ReconstructWithNormals().
//   3. Applying PatchClusterMerger (N=6 clusters) to reduce patch fragmentation.
//   4. Running the full Co3NeManifoldStitcher pipeline with UV merging enabled.
//   5. Reporting before/after metrics at each stage.
//   6. Saving OBJ output files so the results can be inspected.
//
// Run from the repository root:
//   ./test_patch_cluster_xyz [path/to/file.xyz] [numClusters]
// Defaults: r768_1000.xyz, numClusters=6

#include <GTE/Mathematics/Co3Ne.h>
#include <GTE/Mathematics/PatchClusterMerger.h>
#include <GTE/Mathematics/Co3NeManifoldStitcher.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <vector>

using namespace gte;

// -----------------------------------------------------------------------
// IO helpers
// -----------------------------------------------------------------------

struct PointCloud
{
    std::vector<Vector3<double>> positions;
    std::vector<Vector3<double>> normals;
};

// Load a 6-column XYZ file (x y z nx ny nz).
// Falls back to 3-column (x y z) if the line has only 3 values.
static PointCloud LoadXYZWithNormals(std::string const& filename)
{
    PointCloud pc;
    std::ifstream file(filename);
    if (!file.is_open())
    {
        std::cerr << "[LoadXYZ] Cannot open " << filename << "\n";
        return pc;
    }

    double x, y, z, nx, ny, nz;
    while (file >> x >> y >> z)
    {
        pc.positions.push_back({x, y, z});
        if (file >> nx >> ny >> nz)
        {
            pc.normals.push_back({nx, ny, nz});
        }
        else
        {
            pc.normals.push_back({0.0, 0.0, 1.0});  // dummy normal
        }
    }
    return pc;
}

static void SaveOBJ(std::string const& filename,
                    std::vector<Vector3<double>> const& vertices,
                    std::vector<std::array<int32_t, 3>> const& triangles)
{
    std::ofstream f(filename);
    if (!f.is_open())
    {
        std::cerr << "[SaveOBJ] Cannot open " << filename << "\n";
        return;
    }
    for (auto const& v : vertices)
    {
        f << "v " << v[0] << " " << v[1] << " " << v[2] << "\n";
    }
    for (auto const& t : triangles)
    {
        f << "f " << (t[0]+1) << " " << (t[1]+1) << " " << (t[2]+1) << "\n";
    }
    std::cout << "[SaveOBJ] Saved " << triangles.size() << " triangles to "
              << filename << "\n";
}

// -----------------------------------------------------------------------
// Mesh metrics
// -----------------------------------------------------------------------

struct MeshMetrics
{
    int triangles = 0;
    int vertices  = 0;
    int components = 0;
    int boundaryEdges = 0;
    int nonManifoldEdges = 0;
};

static MeshMetrics ComputeMetrics(
    std::vector<Vector3<double>> const& vertices,
    std::vector<std::array<int32_t, 3>> const& triangles)
{
    MeshMetrics m;
    m.triangles = static_cast<int>(triangles.size());
    m.vertices  = static_cast<int>(vertices.size());

    if (triangles.empty())
    {
        return m;
    }

    std::map<std::pair<int32_t,int32_t>, int32_t> edgeCnt;
    for (auto const& tri : triangles)
    {
        for (int k = 0; k < 3; ++k)
        {
            int32_t a = tri[k], b = tri[(k+1)%3];
            if (a > b) std::swap(a, b);
            ++edgeCnt[{a, b}];
        }
    }

    for (auto const& e : edgeCnt)
    {
        if (e.second == 1) ++m.boundaryEdges;
        else if (e.second > 2) ++m.nonManifoldEdges;
    }

    // Connected components (BFS on shared edges).
    std::map<std::pair<int32_t,int32_t>, std::vector<int32_t>> edgeToTris;
    for (int32_t t = 0; t < (int32_t)triangles.size(); ++t)
    {
        for (int k = 0; k < 3; ++k)
        {
            int32_t a = triangles[t][k], b = triangles[t][(k+1)%3];
            if (a > b) std::swap(a, b);
            edgeToTris[{a,b}].push_back(t);
        }
    }
    std::vector<bool> visited(triangles.size(), false);
    for (size_t seed = 0; seed < triangles.size(); ++seed)
    {
        if (visited[seed]) continue;
        ++m.components;
        std::vector<int32_t> stack = {(int32_t)seed};
        visited[seed] = true;
        while (!stack.empty())
        {
            int32_t cur = stack.back(); stack.pop_back();
            for (int k = 0; k < 3; ++k)
            {
                int32_t a = triangles[cur][k], b = triangles[cur][(k+1)%3];
                if (a > b) std::swap(a, b);
                auto it = edgeToTris.find({a,b});
                if (it == edgeToTris.end()) continue;
                for (int32_t nb : it->second)
                {
                    if (!visited[nb]) { visited[nb] = true; stack.push_back(nb); }
                }
            }
        }
    }

    return m;
}

static void PrintMetrics(char const* label, MeshMetrics const& m)
{
    std::cout << "  [" << label << "]\n"
              << "    Triangles:         " << m.triangles       << "\n"
              << "    Vertices:          " << m.vertices        << "\n"
              << "    Components:        " << m.components      << "\n"
              << "    Boundary edges:    " << m.boundaryEdges   << "\n"
              << "    Non-manifold edges:" << m.nonManifoldEdges << "\n";
}

// -----------------------------------------------------------------------
// Main pipeline
// -----------------------------------------------------------------------

int main(int argc, char* argv[])
{
    std::string xyzFile = "r768_1000.xyz";
    int numClusters = 6;

    if (argc >= 2) xyzFile = argv[1];
    if (argc >= 3) numClusters = std::atoi(argv[2]);

    std::cout << "=== PatchClusterMerger XYZ Pipeline Test ===\n"
              << "Input:       " << xyzFile       << "\n"
              << "NumClusters: " << numClusters    << "\n\n";

    // ------------------------------------------------------------------
    // Step 1: Load point cloud
    // ------------------------------------------------------------------
    std::cout << "--- Step 1: Loading " << xyzFile << " ---\n";
    PointCloud pc = LoadXYZWithNormals(xyzFile);
    if (pc.positions.empty())
    {
        std::cerr << "ERROR: No points loaded.\n";
        return 1;
    }
    std::cout << "  Loaded " << pc.positions.size() << " points";
    bool hasNormals = !pc.normals.empty();
    std::cout << (hasNormals ? " + normals\n" : " (no normals)\n");

    // ------------------------------------------------------------------
    // Step 2: Co3Ne reconstruction
    // ------------------------------------------------------------------
    std::cout << "\n--- Step 2: Co3Ne Reconstruction ---\n";
    auto t0 = std::chrono::steady_clock::now();

    Co3Ne<double>::Parameters co3Params;
    co3Params.kNeighbors               = 20;
    co3Params.relaxedManifoldExtraction = true;
    co3Params.orientNormals             = true;
    co3Params.triangleQualityThreshold  = 0.1;

    std::vector<Vector3<double>> verts;
    std::vector<std::array<int32_t, 3>> tris;

    bool ok;
    if (hasNormals)
    {
        std::cout << "  Using precomputed normals (Co3Ne::ReconstructWithNormals)\n";
        ok = Co3Ne<double>::ReconstructWithNormals(
            pc.positions, pc.normals, verts, tris, co3Params);
    }
    else
    {
        std::cout << "  Computing normals via PCA (Co3Ne::Reconstruct)\n";
        ok = Co3Ne<double>::Reconstruct(pc.positions, verts, tris, co3Params);
    }

    auto t1 = std::chrono::steady_clock::now();
    double co3neMs = std::chrono::duration<double, std::milli>(t1 - t0).count();

    if (!ok || tris.empty())
    {
        std::cerr << "ERROR: Co3Ne reconstruction failed or produced no triangles.\n";
        // Try again without precomputed normals as fallback
        if (hasNormals)
        {
            std::cout << "  Retrying without precomputed normals...\n";
            ok = Co3Ne<double>::Reconstruct(pc.positions, verts, tris, co3Params);
            if (!ok || tris.empty())
            {
                std::cerr << "ERROR: Fallback also failed.\n";
                return 1;
            }
        }
        else
        {
            return 1;
        }
    }

    MeshMetrics afterCo3Ne = ComputeMetrics(verts, tris);
    std::cout << "  Co3Ne time: " << co3neMs << " ms\n";
    PrintMetrics("After Co3Ne", afterCo3Ne);
    SaveOBJ("patch_cluster_step1_co3ne.obj", verts, tris);

    // ------------------------------------------------------------------
    // Step 3: PatchClusterMerger – reduce patch fragmentation
    // ------------------------------------------------------------------
    std::cout << "\n--- Step 3: PatchClusterMerger (N=" << numClusters << " clusters) ---\n";
    auto t2 = std::chrono::steady_clock::now();

    PatchClusterMerger<double>::Parameters mergeParams;
    mergeParams.numClusters        = numClusters;
    mergeParams.minPatchesPerCluster = 2;
    mergeParams.verbose            = true;

    std::vector<Vector3<double>>          mergeVerts = verts;
    std::vector<std::array<int32_t, 3>>   mergeTris  = tris;

    int32_t clustersActuallyMerged = PatchClusterMerger<double>::MergeClusteredPatches(
        mergeVerts, mergeTris, mergeParams);

    auto t3 = std::chrono::steady_clock::now();
    double mergeMs = std::chrono::duration<double, std::milli>(t3 - t2).count();

    MeshMetrics afterMerge = ComputeMetrics(mergeVerts, mergeTris);
    std::cout << "  Merge time: " << mergeMs << " ms\n"
              << "  Clusters merged: " << clustersActuallyMerged << "\n";
    PrintMetrics("After PatchClusterMerger", afterMerge);
    SaveOBJ("patch_cluster_step2_merged.obj", mergeVerts, mergeTris);

    // ------------------------------------------------------------------
    // Step 4: Full StitchPatches starting from the merged mesh.
    // This shows how the full pipeline benefits from the cluster reduction.
    // ------------------------------------------------------------------
    std::cout << "\n--- Step 4: StitchPatches on merged mesh ---\n";
    auto t4 = std::chrono::steady_clock::now();

    Co3NeManifoldStitcher<double>::Parameters spParams;
    spParams.verbose                   = false;  // Reduce verbose noise for Step 4
    spParams.enableUVMerging           = false;  // Merger already done in Step 3
    spParams.numClusters               = numClusters;
    spParams.enableHoleFilling         = true;
    spParams.maxHoleEdges              = 100;
    spParams.enableBallPivotHoleFiller = true;
    spParams.enableIterativeBridging   = true;
    spParams.removeNonManifoldEdges    = true;

    // Start from the cluster-merged mesh (not the original Co3Ne output).
    std::vector<Vector3<double>>        finalVerts = mergeVerts;
    std::vector<std::array<int32_t, 3>> finalTris  = mergeTris;

    bool isManifold = Co3NeManifoldStitcher<double>::StitchPatches(
        finalVerts, finalTris, spParams);

    auto t5 = std::chrono::steady_clock::now();
    double stitchMs = std::chrono::duration<double, std::milli>(t5 - t4).count();

    MeshMetrics afterStitch = ComputeMetrics(finalVerts, finalTris);
    std::cout << "  StitchPatches time: " << stitchMs << " ms\n";
    PrintMetrics("After StitchPatches", afterStitch);
    SaveOBJ("patch_cluster_step3_stitched.obj", finalVerts, finalTris);

    // ------------------------------------------------------------------
    // Summary
    // ------------------------------------------------------------------
    std::cout << "\n=== Pipeline Summary ===\n";
    std::cout << "  Input points:        " << pc.positions.size() << "\n";
    PrintMetrics("After Co3Ne",            afterCo3Ne);
    PrintMetrics("After PatchClusterMerger", afterMerge);
    PrintMetrics("After StitchPatches",    afterStitch);

    std::cout << "\n  Manifold result:     " << (isManifold ? "YES" : "NO") << "\n";
    std::cout << "  Patches reduced from " << afterCo3Ne.components
              << " → " << afterMerge.components
              << " → " << afterStitch.components << " components\n";
    std::cout << "\nOutput files:\n"
              << "  patch_cluster_step1_co3ne.obj\n"
              << "  patch_cluster_step2_merged.obj\n"
              << "  patch_cluster_step3_stitched.obj\n";

    // Return success if we at least got triangles out.
    return (afterStitch.triangles > 0) ? 0 : 1;
}
