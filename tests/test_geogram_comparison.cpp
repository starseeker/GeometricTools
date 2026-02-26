// Comparison test: GTE vs Geogram mesh processing
//
// This test runs the same mesh repair, hole-filling, and surface reconstruction
// operations using both Geogram and GTE implementations, then compares the output
// quality metrics to verify that the GTE port is functionally equivalent to Geogram.
//
// BRL-CAD uses Geogram for three purposes:
//   1. Mesh repair (repair.cpp)     - MeshRepair + MeshHoleFilling
//   2. Mesh remeshing (remesh.cpp)  - MeshRemesh (CVT-based)
//   3. Surface reconstruction       - Co3Ne point cloud → mesh
//
// This test validates use cases 1 and 3.
//
// Geogram submodule: geogram/
// GTE implementations: GTE/Mathematics/MeshRepair.h, MeshHoleFilling.h, Co3Ne.h
//
// Usage:
//   ./test_geogram_comparison <input.obj> [points.xyz]
//     input.obj   - mesh for repair+hole-filling test
//     points.xyz  - optional point cloud (x y z nx ny nz) for Co3Ne test
//
// Success criteria:
//   Repair/fill: GTE fills at least as many holes as Geogram, volume diff < VOLUME_TOLERANCE_PCT%
//   Co3Ne:       GTE produces non-empty output with plausible triangle count

#include <GTE/Mathematics/MeshRepair.h>
#include <GTE/Mathematics/MeshHoleFilling.h>
#include <GTE/Mathematics/MeshValidation.h>
#include <GTE/Mathematics/Co3Ne.h>

// Geogram includes
#include <geogram/basic/process.h>
#include <geogram/basic/command_line.h>
#include <geogram/basic/command_line_args.h>
#include <geogram/mesh/mesh.h>
#include <geogram/mesh/mesh_geometry.h>
#include <geogram/mesh/mesh_preprocessing.h>
#include <geogram/mesh/mesh_repair.h>
#include <geogram/mesh/mesh_fill_holes.h>
#include <geogram/points/co3ne.h>
#include <geogram/basic/attributes.h>

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

// Co3Ne comparison: search radius as fraction of bounding box diagonal (mirrors BRL-CAD co3ne.cpp)
static constexpr double CO3NE_SEARCH_RADIUS_FRACTION = 0.05;
// Co3Ne: maximum normal angle in degrees for co-cone filter (mirrors BRL-CAD co3ne.cpp "co3ne:max_N_angle" = 60.0)
static constexpr double CO3NE_MAX_NORMAL_ANGLE_DEG   = 60.0;
// Co3Ne triangle count ratio bounds: acceptable range for GTE vs Geogram output triangle count.
// A wide range (0.5x – 3.0x) is expected because the two implementations use different neighbor
// counts, search radius heuristics, and T3/T12 classification strategies while still producing
// plausible reconstructions of the same surface.
static constexpr double CO3NE_TRI_RATIO_MIN          = 0.5;
static constexpr double CO3NE_TRI_RATIO_MAX          = 3.0;

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

// Load point cloud in "x y z nx ny nz" format (BRL-CAD co3ne.cpp input format)
bool LoadXYZPointCloud(std::string const& filename,
    std::vector<Vector3<double>>& points,
    std::vector<Vector3<double>>& normals)
{
    std::ifstream file(filename);
    if (!file.is_open()) { return false; }

    points.clear();
    normals.clear();

    std::string line;
    while (std::getline(file, line))
    {
        std::istringstream iss(line);
        double px, py, pz, nx, ny, nz;
        if (iss >> px >> py >> pz >> nx >> ny >> nz)
        {
            points.push_back(Vector3<double>{px, py, pz});
            normals.push_back(Vector3<double>{nx, ny, nz});
        }
    }
    return !points.empty();
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

// ---- Co3Ne comparison functions ----

// Run Geogram Co3Ne_reconstruct on the given point cloud.
// Mirrors BRL-CAD co3ne.cpp usage exactly:
//   gm.vertices.assign_points(...)
//   normals attribute filled from input
//   search_dist = 0.05 * GEO::bbox_diagonal(gm)
//   GEO::Co3Ne_reconstruct(gm, search_dist)
bool RunGeogramCo3Ne(
    std::vector<Vector3<double>> const& points,
    std::vector<Vector3<double>> const& normals,
    std::vector<Vector3<double>>& outVertices,
    std::vector<std::array<int32_t, 3>>& outTriangles)
{
    GEO::Mesh gm;

    // Assign points
    std::vector<double> flat(points.size() * 3);
    for (size_t i = 0; i < points.size(); ++i)
    {
        flat[3*i+0] = points[i][0];
        flat[3*i+1] = points[i][1];
        flat[3*i+2] = points[i][2];
    }
    gm.vertices.assign_points(flat.data(), 3, static_cast<GEO::index_t>(points.size()));

    // Create and fill normal attribute
    GEO::Attribute<double> normalAttr;
    normalAttr.create_vector_attribute(gm.vertices.attributes(), "normal", 3);
    for (size_t i = 0; i < normals.size(); ++i)
    {
        normalAttr[3*i+0] = normals[i][0];
        normalAttr[3*i+1] = normals[i][1];
        normalAttr[3*i+2] = normals[i][2];
    }

    double search_dist = CO3NE_SEARCH_RADIUS_FRACTION * GEO::bbox_diagonal(gm);
    GEO::Co3Ne_reconstruct(gm, search_dist);

    // Extract results
    outVertices.clear();
    outTriangles.clear();
    for (GEO::index_t v = 0; v < gm.vertices.nb(); ++v)
    {
        double const* pt = gm.vertices.point_ptr(v);
        outVertices.push_back(Vector3<double>{pt[0], pt[1], pt[2]});
    }
    for (GEO::index_t f = 0; f < gm.facets.nb(); ++f)
    {
        if (gm.facets.nb_vertices(f) == 3)
        {
            outTriangles.push_back({
                static_cast<int32_t>(gm.facets.vertex(f, 0)),
                static_cast<int32_t>(gm.facets.vertex(f, 1)),
                static_cast<int32_t>(gm.facets.vertex(f, 2))
            });
        }
    }
    return !outVertices.empty();
}

// Run GTE Co3Ne::Reconstruct on the given point cloud.
// GTE Co3Ne estimates normals internally via PCA; the pre-computed normals from
// the input file are intentionally not passed because the GTE API takes only
// positions and derives normals from the point neighborhood structure.
bool RunGTECo3Ne(
    std::vector<Vector3<double>> const& points,
    std::vector<Vector3<double>> const& /*normals - estimated internally by GTE via PCA*/,
    std::vector<Vector3<double>>& outVertices,
    std::vector<std::array<int32_t, 3>>& outTriangles)
{
    Co3Ne<double>::Parameters params;
    params.kNeighbors     = 20;
    // searchRadius = 0 triggers automatic calculation based on bounding box diagonal,
    // analogous to CO3NE_SEARCH_RADIUS_FRACTION used in the Geogram path.
    params.searchRadius   = static_cast<double>(0);
    params.maxNormalAngle = static_cast<double>(CO3NE_MAX_NORMAL_ANGLE_DEG);
    params.orientNormals  = true;
    params.smoothWithRVD  = false;

    return Co3Ne<double>::Reconstruct(points, outVertices, outTriangles, params);
}

// Run and compare Co3Ne results. Returns true if GTE passes all checks.
bool RunCo3NeComparison(std::string const& xyzFile)
{
    std::cout << "\n--- Co3Ne Surface Reconstruction ---\n";
    std::cout << "Input: " << xyzFile << "\n";

    std::vector<Vector3<double>> points, normals;
    if (!LoadXYZPointCloud(xyzFile, points, normals))
    {
        std::cerr << "[Co3Ne] Failed to load " << xyzFile << "\n";
        return false;
    }
    std::cout << "  Points loaded: " << points.size() << "\n";

    // Run Geogram Co3Ne
    std::cout << "Running Geogram Co3Ne...\n";
    std::vector<Vector3<double>> geoVerts, gteVerts;
    std::vector<std::array<int32_t, 3>> geoTris, gteTris;

    bool geoOK = RunGeogramCo3Ne(points, normals, geoVerts, geoTris);
    if (!geoOK) { std::cerr << "[Co3Ne] Geogram returned empty mesh\n"; return false; }

    // Run GTE Co3Ne
    std::cout << "Running GTE Co3Ne...\n";
    bool gteOK = RunGTECo3Ne(points, normals, gteVerts, gteTris);

    // Validate and compare
    auto geoValid = MeshValidation<double>::Validate(geoVerts, geoTris, false);
    auto gteValid = gteOK
        ? MeshValidation<double>::Validate(gteVerts, gteTris, false)
        : MeshValidation<double>::ValidationResult{};

    std::cout << "\nCo3Ne Results:\n";
    std::cout << std::left
              << std::setw(22) << "Metric"
              << std::setw(16) << "Geogram"
              << std::setw(16) << "GTE"
              << "\n";
    std::cout << std::string(54, '-') << "\n";
    std::cout << std::left  << std::setw(22) << "Triangles"
              << std::right << std::setw(16) << geoTris.size()
              << std::setw(16) << gteTris.size() << "\n";
    std::cout << std::left  << std::setw(22) << "Boundary Edges"
              << std::right << std::setw(16) << geoValid.boundaryEdges
              << std::setw(16) << gteValid.boundaryEdges << "\n";

    bool passed = true;

    // GTE must produce non-empty output
    bool gteNonEmpty = gteOK && !gteTris.empty();
    std::cout << "GTE non-empty:   " << (gteNonEmpty ? "PASS" : "FAIL") << "\n";
    if (!gteNonEmpty) { passed = false; }

    // GTE triangle count should be within CO3NE_TRI_RATIO_MIN – CO3NE_TRI_RATIO_MAX of Geogram's.
    // A wide range is expected: the two implementations use different neighbor counts,
    // search radius heuristics, and T3/T12 classification strategies.
    if (gteNonEmpty && !geoTris.empty())
    {
        double ratio = static_cast<double>(gteTris.size()) / static_cast<double>(geoTris.size());
        bool countOK = (ratio >= CO3NE_TRI_RATIO_MIN && ratio <= CO3NE_TRI_RATIO_MAX);
        std::cout << "GTE triangle count ratio vs Geogram: " << ratio
                  << " — " << (countOK ? "PASS" : "FAIL") << "\n";
        if (!countOK) { passed = false; }
    }

    SaveOBJ("/tmp/co3ne_gte_output.obj",    gteVerts, gteTris);
    SaveOBJ("/tmp/co3ne_geogram_output.obj", geoVerts, geoTris);
    std::cout << "Outputs saved to /tmp/co3ne_gte_output.obj and /tmp/co3ne_geogram_output.obj\n";

    return passed;
}

// ---- Main comparison logic ----

int main(int argc, char* argv[])
{
    std::cout << std::fixed << std::setprecision(4);

    if (argc < 2)
    {
        std::cerr << "Usage: " << argv[0] << " <input.obj> [points.xyz]\n";
        std::cerr << "  input.obj   - mesh for repair + hole-filling test\n";
        std::cerr << "  points.xyz  - optional point cloud for Co3Ne test (x y z nx ny nz)\n";
        return 1;
    }

    std::string inputFile = argv[1];
    std::string xyzFile   = (argc >= 3) ? argv[2] : "";

    // Initialize Geogram
    GEO::initialize();
    GEO::Logger::instance()->unregister_all_clients();
    GEO::CmdLine::import_arg_group("standard");
    GEO::CmdLine::import_arg_group("algo");
    GEO::CmdLine::import_arg_group("co3ne");
    GEO::CmdLine::set_arg("co3ne:max_N_angle", std::to_string(CO3NE_MAX_NORMAL_ANGLE_DEG));

    std::cout << "=== GTE vs Geogram Mesh Processing Comparison ===\n\n";
    std::cout << "--- Use Case 1: Mesh Repair + Hole Filling ---\n";
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

    // Save repair+fill outputs for inspection
    SaveOBJ("/tmp/gte_output.obj",    gteVerts, gteTris);
    SaveOBJ("/tmp/geogram_output.obj", geoVerts, geoTris);
    std::cout << "\nOutputs saved to /tmp/gte_output.obj and /tmp/geogram_output.obj\n";

    // --- Use Case 3: Co3Ne surface reconstruction (optional) ---
    bool co3nePassed = true;
    if (!xyzFile.empty())
    {
        co3nePassed = RunCo3NeComparison(xyzFile);
        if (co3nePassed)
            std::cout << "Co3Ne comparison: PASS\n";
        else
        {
            std::cout << "Co3Ne comparison: FAIL\n";
            allPassed = false;
        }
    }

    std::cout << "\n=== OVERALL: " << (allPassed ? "PASS" : "FAIL") << " ===\n";

    return allPassed ? 0 : 1;
}
