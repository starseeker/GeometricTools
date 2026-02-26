// Comparison test: GTE vs Geogram mesh processing
//
// This test runs the same mesh repair and hole-filling operations using both
// Geogram and GTE implementations, then compares the output quality metrics
// to verify that the GTE port is functionally equivalent to Geogram.
//
// Geogram submodule: geogram/
// GTE implementations: GTE/Mathematics/MeshRepair.h, MeshHoleFilling.h
//
// Usage:
//   ./test_geogram_comparison <input.obj>
//
// Success criteria:
//   - GTE fills at least as many holes as Geogram (fewer or equal boundary edges)
//   - Volume difference < VOLUME_TOLERANCE_PCT%
//   - Surface area difference < AREA_TOLERANCE_PCT%

#include <GTE/Mathematics/MeshRepair.h>
#include <GTE/Mathematics/MeshHoleFilling.h>
#include <GTE/Mathematics/MeshValidation.h>

// Geogram includes
#include <geogram/basic/process.h>
#include <geogram/basic/command_line.h>
#include <geogram/basic/command_line_args.h>
#include <geogram/mesh/mesh.h>
#include <geogram/mesh/mesh_geometry.h>
#include <geogram/mesh/mesh_preprocessing.h>
#include <geogram/mesh/mesh_repair.h>
#include <geogram/mesh/mesh_fill_holes.h>

#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <cmath>
#include <vector>
#include <array>
#include <string>

using namespace gte;

// ---- Thresholds and constants ----

// Geogram pipeline parameters (mirror BRL-CAD repair.cpp)
static constexpr double GEO_EPSILON_SCALE        = 1e-6 * 0.01; // epsilon = scale * bbox_diag
static constexpr double GEO_SMALL_COMP_FRACTION  = 0.03;        // remove components < 3% of total area
static constexpr double GEO_HOLE_FILL_FRACTION   = 0.10;        // fill holes with perimeter < 10% of bbox_diag

// GTE pipeline parameters (mirror Geogram defaults)
static constexpr double GTE_EPSILON              = 1e-6;
static constexpr double GTE_SMALL_COMP_FRACTION  = 0.03;        // same 3% threshold as Geogram

// Assessment tolerances
static constexpr double VOLUME_TOLERANCE_PCT     = 10.0;        // volume must match within 10%
static constexpr double AREA_TOLERANCE_PCT_BASE  = 15.0;        // base area tolerance (15%)
// When GTE fills more holes, the extra triangulated patches add surface area.
// In that case a relaxed tolerance is applied.
static constexpr double AREA_TOLERANCE_PCT_EXTRA = 30.0;

// ---- OBJ I/O ----

bool LoadOBJ(std::string const& filename,
    std::vector<Vector3<double>>& vertices,
    std::vector<std::array<int32_t, 3>>& triangles)
{
    std::ifstream file(filename);
    if (!file.is_open()) { return false; }

    vertices.clear();
    triangles.clear();

    std::string line;
    while (std::getline(file, line))
    {
        std::istringstream iss(line);
        std::string prefix;
        iss >> prefix;
        if (prefix == "v")
        {
            double x, y, z;
            iss >> x >> y >> z;
            vertices.push_back(Vector3<double>{x, y, z});
        }
        else if (prefix == "f")
        {
            // Handle "f v1 v2 v3" (1-indexed, optional "/tc/n" suffixes)
            std::array<int32_t, 3> tri{};
            for (int i = 0; i < 3; ++i)
            {
                std::string token;
                if (!(iss >> token)) { return false; }
                tri[i] = std::stoi(token) - 1; // convert 1-based to 0-based
            }
            triangles.push_back(tri);
        }
    }
    return !vertices.empty();
}

void SaveOBJ(std::string const& filename,
    std::vector<Vector3<double>> const& vertices,
    std::vector<std::array<int32_t, 3>> const& triangles)
{
    std::ofstream file(filename);
    for (auto const& v : vertices)
    {
        file << "v " << v[0] << " " << v[1] << " " << v[2] << "\n";
    }
    for (auto const& t : triangles)
    {
        file << "f " << (t[0]+1) << " " << (t[1]+1) << " " << (t[2]+1) << "\n";
    }
}

// ---- Mesh metrics ----

double ComputeVolume(std::vector<Vector3<double>> const& verts,
    std::vector<std::array<int32_t, 3>> const& tris)
{
    double vol = 0.0;
    for (auto const& t : tris)
    {
        auto const& v0 = verts[t[0]];
        auto const& v1 = verts[t[1]];
        auto const& v2 = verts[t[2]];
        vol += Dot(v0, Cross(v1, v2));
    }
    return std::abs(vol / 6.0);
}

double ComputeSurfaceArea(std::vector<Vector3<double>> const& verts,
    std::vector<std::array<int32_t, 3>> const& tris)
{
    double area = 0.0;
    for (auto const& t : tris)
    {
        auto const& v0 = verts[t[0]];
        auto const& v1 = verts[t[1]];
        auto const& v2 = verts[t[2]];
        area += Length(Cross(v1 - v0, v2 - v0)) * 0.5;
    }
    return area;
}

// ---- Geogram helpers ----

// Manually populate GEO::Mesh from vertices/triangles.
// Uses create_polygon (not create_triangle) to avoid premature connect() calls;
// mesh_repair() will build the adjacency information.
bool PopulateGeogramMesh(
    std::vector<Vector3<double>> const& vertices,
    std::vector<std::array<int32_t, 3>> const& triangles,
    GEO::Mesh& gm)
{
    gm.clear();
    gm.vertices.assign_points(
        reinterpret_cast<double const*>(vertices.data()),
        3,
        static_cast<GEO::index_t>(vertices.size())
    );

    for (auto const& tri : triangles)
    {
        GEO::index_t f = gm.facets.create_polygon(3);
        gm.facets.set_vertex(f, 0, static_cast<GEO::index_t>(tri[0]));
        gm.facets.set_vertex(f, 1, static_cast<GEO::index_t>(tri[1]));
        gm.facets.set_vertex(f, 2, static_cast<GEO::index_t>(tri[2]));
    }
    return true;
}

// Extract vertices and triangles from a GEO::Mesh
void GeogramMeshToArrays(GEO::Mesh const& gm,
    std::vector<Vector3<double>>& vertices,
    std::vector<std::array<int32_t, 3>>& triangles)
{
    vertices.clear();
    triangles.clear();

    for (GEO::index_t v = 0; v < gm.vertices.nb(); ++v)
    {
        double const* pt = gm.vertices.point_ptr(v);
        vertices.push_back(Vector3<double>{pt[0], pt[1], pt[2]});
    }

    for (GEO::index_t f = 0; f < gm.facets.nb(); ++f)
    {
        GEO::index_t nb = gm.facets.nb_vertices(f);
        if (nb == 3)
        {
            triangles.push_back({
                static_cast<int32_t>(gm.facets.vertex(f, 0)),
                static_cast<int32_t>(gm.facets.vertex(f, 1)),
                static_cast<int32_t>(gm.facets.vertex(f, 2))
            });
        }
    }
}

// ---- Run Geogram pipeline ----

bool RunGeogramRepairAndFillHoles(
    std::string const& inputFile,
    std::vector<Vector3<double>>& outVertices,
    std::vector<std::array<int32_t, 3>>& outTriangles)
{
    // Load OBJ ourselves (avoids geogram OBJ loader quirks)
    std::vector<Vector3<double>> inputVerts;
    std::vector<std::array<int32_t, 3>> inputTris;
    if (!LoadOBJ(inputFile, inputVerts, inputTris))
    {
        std::cerr << "[Geogram] Failed to load " << inputFile << "\n";
        return false;
    }

    GEO::Mesh gm;
    if (!PopulateGeogramMesh(inputVerts, inputTris, gm))
    {
        std::cerr << "[Geogram] Failed to populate mesh\n";
        return false;
    }

    // Mirror BRL-CAD's repair.cpp usage
    double bbox_diag = GEO::bbox_diagonal(gm);
    double epsilon = GEO_EPSILON_SCALE * bbox_diag;

    GEO::mesh_repair(gm, GEO::MeshRepairMode(GEO::MESH_REPAIR_DEFAULT), epsilon);

    double area = GEO::Geom::mesh_area(gm, 3);
    double min_comp_area = GEO_SMALL_COMP_FRACTION * area;
    GEO::remove_small_connected_components(gm, min_comp_area);

    double hole_size = GEO_HOLE_FILL_FRACTION * GEO::bbox_diagonal(gm);
    GEO::fill_holes(gm, hole_size);

    GEO::mesh_repair(gm, GEO::MeshRepairMode(GEO::MESH_REPAIR_DEFAULT));

    GeogramMeshToArrays(gm, outVertices, outTriangles);
    return true;
}

// ---- Run GTE pipeline ----

bool RunGTERepairAndFillHoles(
    std::string const& inputFile,
    std::vector<Vector3<double>>& outVertices,
    std::vector<std::array<int32_t, 3>>& outTriangles)
{
    if (!LoadOBJ(inputFile, outVertices, outTriangles))
    {
        std::cerr << "[GTE] Failed to load " << inputFile << "\n";
        return false;
    }

    // Repair - mirror Geogram's MESH_REPAIR_DEFAULT (colocate + remove dup_f)
    MeshRepair<double>::Parameters repairParams;
    repairParams.epsilon = GTE_EPSILON;
    MeshRepair<double>::Repair(outVertices, outTriangles, repairParams);

    // Compute total area to determine small-component threshold
    double totalArea = ComputeSurfaceArea(outVertices, outTriangles);
    double minCompArea = GTE_SMALL_COMP_FRACTION * totalArea;
    MeshPreprocessing<double>::RemoveSmallComponents(outVertices, outTriangles, minCompArea);

    // Second repair pass after component removal (mirrors BRL-CAD bot_to_geogram)
    MeshRepair<double>::Repair(outVertices, outTriangles, repairParams);

    // Fill holes - use CDT with auto-fallback (best quality)
    MeshHoleFilling<double>::Parameters fillParams;
    fillParams.method = MeshHoleFilling<double>::TriangulationMethod::CDT;
    fillParams.autoFallback = true;
    fillParams.validateOutput = false;
    MeshHoleFilling<double>::FillHoles(outVertices, outTriangles, fillParams);

    // Final repair pass
    MeshRepair<double>::Repair(outVertices, outTriangles, repairParams);

    return true;
}

// ---- Main comparison logic ----

int main(int argc, char* argv[])
{
    std::cout << std::fixed << std::setprecision(4);

    if (argc < 2)
    {
        std::cerr << "Usage: " << argv[0] << " <input.obj>\n";
        return 1;
    }

    std::string inputFile = argv[1];

    // Initialize Geogram
    GEO::initialize();
    GEO::Logger::instance()->unregister_all_clients();
    GEO::CmdLine::import_arg_group("standard");
    GEO::CmdLine::import_arg_group("algo");

    std::cout << "=== GTE vs Geogram Mesh Processing Comparison ===\n\n";
    std::cout << "Input: " << inputFile << "\n\n";

    // --- Run Geogram ---
    std::cout << "Running Geogram pipeline...\n";
    std::vector<Vector3<double>> geoVerts, gteVerts;
    std::vector<std::array<int32_t, 3>> geoTris, gteTris;

    bool geoOK = RunGeogramRepairAndFillHoles(inputFile, geoVerts, geoTris);
    if (!geoOK)
    {
        std::cerr << "ERROR: Geogram pipeline failed\n";
        return 1;
    }

    // --- Run GTE ---
    std::cout << "Running GTE pipeline...\n";
    bool gteOK = RunGTERepairAndFillHoles(inputFile, gteVerts, gteTris);
    if (!gteOK)
    {
        std::cerr << "ERROR: GTE pipeline failed\n";
        return 1;
    }

    // --- Compute metrics ---
    double geoVol  = ComputeVolume(geoVerts, geoTris);
    double geoArea = ComputeSurfaceArea(geoVerts, geoTris);
    auto geoValid  = MeshValidation<double>::Validate(geoVerts, geoTris, false);

    double gteVol  = ComputeVolume(gteVerts, gteTris);
    double gteArea = ComputeSurfaceArea(gteVerts, gteTris);
    auto gteValid  = MeshValidation<double>::Validate(gteVerts, gteTris, false);

    // --- Print comparison table ---
    std::cout << "\n=== Results ===\n";
    std::cout << std::left
              << std::setw(22) << "Metric"
              << std::setw(16) << "Geogram"
              << std::setw(16) << "GTE"
              << std::setw(12) << "Diff %"
              << "\n";
    std::cout << std::string(66, '-') << "\n";

    auto row = [&](std::string const& name, double geo, double gte)
    {
        double pct = (geo > 1e-10) ? 100.0 * (gte - geo) / geo : 0.0;
        std::cout << std::left  << std::setw(22) << name
                  << std::right << std::setw(16) << geo
                  << std::setw(16) << gte
                  << std::setw(11) << pct << "%\n";
    };

    row("Vertices",     (double)geoVerts.size(),  (double)gteVerts.size());
    row("Triangles",    (double)geoTris.size(),   (double)gteTris.size());
    row("Volume",       geoVol,  gteVol);
    row("Surface Area", geoArea, gteArea);
    row("Boundary Edges", (double)geoValid.boundaryEdges, (double)gteValid.boundaryEdges);

    // --- Assess correctness ---
    std::cout << "\n=== Assessment ===\n";

    bool allPassed = true;

    // Manifold check
    bool geoManifold = (geoValid.boundaryEdges == 0);
    bool gteManifold = (gteValid.boundaryEdges == 0);
    if (geoManifold)
        std::cout << "Geogram manifold: PASS (0 boundary edges)\n";
    else
        std::cout << "Geogram manifold: NOTE (" << geoValid.boundaryEdges << " boundary edges)\n";

    if (gteManifold)
        std::cout << "GTE manifold:     PASS (0 boundary edges)\n";
    else
        std::cout << "GTE manifold:     NOTE (" << gteValid.boundaryEdges << " boundary edges)\n";

    // Hole-filling quality: GTE should produce fewer or equal boundary edges
    bool gteFilledBetter = (gteValid.boundaryEdges <= geoValid.boundaryEdges);
    std::cout << "GTE hole-filling: " << (gteFilledBetter ? "PASS" : "FAIL")
              << " (GTE boundary=" << gteValid.boundaryEdges
              << ", Geogram boundary=" << geoValid.boundaryEdges << ")\n";
    if (!gteFilledBetter) { allPassed = false; }

    // Volume comparison (primary quality metric)
    double volPct = 0.0;
    if (geoVol > 1e-10)
    {
        volPct = std::abs(100.0 * (gteVol - geoVol) / geoVol);
    }
    bool volOK = (volPct < VOLUME_TOLERANCE_PCT);
    std::cout << "Volume match:     " << (volOK ? "PASS" : "FAIL")
              << " (diff = " << volPct << "%, threshold " << VOLUME_TOLERANCE_PCT << "%)\n";
    if (!volOK) { allPassed = false; }

    // Surface area comparison: allow larger tolerance when GTE fills more holes
    // than Geogram (more filled holes = more surface area added)
    double areaPct = 0.0;
    if (geoArea > 1e-10)
    {
        areaPct = std::abs(100.0 * (gteArea - geoArea) / geoArea);
    }
    // If GTE filled more holes, use relaxed tolerance; otherwise use base tolerance
    double areaTolerance = (gteFilledBetter && !gteManifold && !geoManifold)
        ? AREA_TOLERANCE_PCT_EXTRA : AREA_TOLERANCE_PCT_BASE;
    bool areaOK = (areaPct < areaTolerance);
    std::cout << "Surface area match: " << (areaOK ? "PASS" : "FAIL")
              << " (diff = " << areaPct << "%, threshold " << areaTolerance << "%)\n";
    if (!areaOK) { allPassed = false; }

    // Non-empty output check
    bool gteHasOutput = (!gteVerts.empty() && !gteTris.empty());
    std::cout << "GTE non-empty output: " << (gteHasOutput ? "PASS" : "FAIL") << "\n";
    if (!gteHasOutput) { allPassed = false; }

    std::cout << "\n=== OVERALL: " << (allPassed ? "PASS" : "FAIL") << " ===\n";

    // Save outputs for inspection
    SaveOBJ("/tmp/gte_output.obj",    gteVerts, gteTris);
    SaveOBJ("/tmp/geogram_output.obj", geoVerts, geoTris);
    std::cout << "\nOutputs saved to /tmp/gte_output.obj and /tmp/geogram_output.obj\n";

    return allPassed ? 0 : 1;
}
