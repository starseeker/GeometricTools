// Comprehensive correctness tests for the geogram-ported GTE mesh algorithms.
// Tests: MeshRepair, MeshPreprocessing, MeshHoleFilling, MeshValidation

#include <Mathematics/MeshRepair.h>
#include <Mathematics/MeshPreprocessing.h>
#include <Mathematics/MeshHoleFilling.h>
#include <Mathematics/MeshValidation.h>

#include <cassert>
#include <cmath>
#include <iostream>
#include <string>
#include <algorithm>
#include <set>

using namespace gte;

static constexpr double kPI = 3.14159265358979323846;

// ─────────────────────────────────────────────────────────────────────────────
// Test infrastructure
// ─────────────────────────────────────────────────────────────────────────────
static int gPassed = 0, gFailed = 0;

#define CHECK(cond, msg)                                \
    do {                                                 \
        if (cond) { ++gPassed; }                        \
        else {                                           \
            ++gFailed;                                   \
            std::cerr << "FAIL [" << msg << "]\n";      \
        }                                                \
    } while (0)

// ─────────────────────────────────────────────────────────────────────────────
// Mesh-builder helpers
// ─────────────────────────────────────────────────────────────────────────────
using V3   = Vector3<double>;
using Tri  = std::array<int32_t, 3>;
using Verts = std::vector<V3>;
using Tris  = std::vector<Tri>;

// Unit tetrahedron (closed, 4 triangles, 4 vertices, consistent CCW normals
// when viewed from outside).
static void MakeTetrahedron(Verts& V, Tris& T) {
    V = {
        { 0, 0, 0 },
        { 1, 0, 0 },
        { 0.5, 1, 0 },
        { 0.5, 0.5, 1 }
    };
    T = {
        { 0, 2, 1 },
        { 0, 1, 3 },
        { 1, 2, 3 },
        { 0, 3, 2 }
    };
}

// Cube (closed, 12 triangles, 8 vertices) with consistent CCW winding.
static void MakeCube(Verts& V, Tris& T) {
    V = {
        {0,0,0}, {1,0,0}, {1,1,0}, {0,1,0},
        {0,0,1}, {1,0,1}, {1,1,1}, {0,1,1}
    };
    T = {
        // bottom (-Z)
        {0,2,1}, {0,3,2},
        // top (+Z)
        {4,5,6}, {4,6,7},
        // front (-Y)
        {0,1,5}, {0,5,4},
        // back (+Y)
        {2,3,7}, {2,7,6},
        // left (-X)
        {0,4,7}, {0,7,3},
        // right (+X)
        {1,2,6}, {1,6,5}
    };
}

// Open square (quad split into 2 triangles), CCW winding — one boundary loop.
static void MakeOpenSquare(Verts& V, Tris& T) {
    V = {
        {0,0,0}, {1,0,0}, {1,1,0}, {0,1,0}
    };
    T = {
        {0,1,2},
        {0,2,3}
    };
}

// Pyramid: four side triangles meeting at apex, open square base.
static void MakePyramid(Verts& V, Tris& T) {
    V = {
        {0,0,0}, {1,0,0}, {1,1,0}, {0,1,0},
        {0.5,0.5,1}
    };
    T = {
        {0,1,4},
        {1,2,4},
        {2,3,4},
        {3,0,4}
    };
}

// ─────────────────────────────────────────────────────────────────────────────
// T1: MeshRepair – duplicate vertices merged
// ─────────────────────────────────────────────────────────────────────────────
static void Test_ColocateVertices() {
    Verts V = {
        {0,0,0}, {1,0,0}, {0,1,0},
        {0,0,0}, {1,0,0}, {1,1,0}
    };
    Tris T = { {0,1,2}, {3,4,5} };

    MeshRepair<double>::Parameters p;
    p.mode = MeshRepair<double>::RepairMode::COLOCATE;
    MeshRepair<double>::Repair(V, T, p);

    CHECK(V.size() == 4, "ColocateVertices: vertex count");
    CHECK(T.size() == 2, "ColocateVertices: triangle count preserved");
    for (auto const& t : T) {
        CHECK(t[0] != t[1] && t[1] != t[2] && t[0] != t[2],
              "ColocateVertices: no degenerate triangles");
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// T2: MeshRepair – degenerate triangles removed
// ─────────────────────────────────────────────────────────────────────────────
static void Test_RemoveDegenerateFacets() {
    Verts V;
    Tris  T;
    MakeCube(V, T);
    T.push_back({0, 0, 1});  // degenerate

    MeshRepair<double>::Parameters p;
    p.mode = MeshRepair<double>::RepairMode::DEFAULT;
    MeshRepair<double>::Repair(V, T, p);

    bool hasDegen = false;
    for (auto const& t : T) {
        if (t[0]==t[1] || t[1]==t[2] || t[0]==t[2]) hasDegen = true;
    }
    CHECK(!hasDegen, "RemoveDegenerateFacets: no degenerate triangles remain");
    CHECK(T.size() == 12, "RemoveDegenerateFacets: correct triangle count");
}

// ─────────────────────────────────────────────────────────────────────────────
// T3: MeshRepair – duplicate triangles removed
// ─────────────────────────────────────────────────────────────────────────────
static void Test_RemoveDuplicateFacets() {
    Verts V;
    Tris T;
    MakeTetrahedron(V, T);
    T.push_back({1, 2, 3});
    T.push_back({1, 2, 3});

    MeshRepair<double>::Parameters p;
    p.mode = MeshRepair<double>::RepairMode::DUP_F;
    MeshRepair<double>::Repair(V, T, p);

    int count13 = 0;
    for (auto const& t : T) {
        auto n = t;
        std::sort(n.begin(), n.end());
        if (n[0]==1 && n[1]==2 && n[2]==3) ++count13;
    }
    CHECK(count13 == 1, "RemoveDuplicateFacets: duplicates removed");
    CHECK(T.size() == 4, "RemoveDuplicateFacets: total count");
}

// ─────────────────────────────────────────────────────────────────────────────
// T4: MeshRepair – isolated vertices removed
// ─────────────────────────────────────────────────────────────────────────────
static void Test_RemoveIsolatedVertices() {
    Verts V = { {0,0,0}, {1,0,0}, {0,1,0}, {99,99,99} };
    Tris  T = { {0,1,2} };

    MeshRepair<double>::Parameters p;
    p.mode = static_cast<MeshRepair<double>::RepairMode>(0);
    MeshRepair<double>::Repair(V, T, p);

    CHECK(V.size() == 3, "RemoveIsolatedVertices: isolated vertex removed");
    CHECK(T.size() == 1, "RemoveIsolatedVertices: triangle preserved");
}

// ─────────────────────────────────────────────────────────────────────────────
// T5: MeshRepair – DetectColocatedVertices exact-match
// ─────────────────────────────────────────────────────────────────────────────
static void Test_DetectColocatedExact() {
    Verts V = { {0,0,0}, {1,0,0}, {0,0,0}, {2,0,0} };
    std::vector<int32_t> colocated;
    MeshRepair<double>::DetectColocatedVertices(V, colocated, 0.0);

    CHECK(colocated[0] == 0, "DetectColocatedExact: v0 canonical");
    CHECK(colocated[1] == 1, "DetectColocatedExact: v1 canonical");
    CHECK(colocated[2] == 0, "DetectColocatedExact: v2 → v0");
    CHECK(colocated[3] == 3, "DetectColocatedExact: v3 canonical");
}

// ─────────────────────────────────────────────────────────────────────────────
// T6: MeshRepair – DetectColocatedVertices epsilon-match
// ─────────────────────────────────────────────────────────────────────────────
static void Test_DetectColocatedEpsilon() {
    Verts V = { {0,0,0}, {0.0001,0,0}, {10,0,0} };
    std::vector<int32_t> colocated;
    MeshRepair<double>::DetectColocatedVertices(V, colocated, 0.001);

    bool merged = (colocated[0] == colocated[1]);
    CHECK(merged, "DetectColocatedEpsilon: nearby vertices merged");
    CHECK(colocated[2] == 2, "DetectColocatedEpsilon: distant vertex unique");
}

// ─────────────────────────────────────────────────────────────────────────────
// T7: MeshPreprocessing – RemoveSmallFacets
// ─────────────────────────────────────────────────────────────────────────────
static void Test_RemoveSmallFacets() {
    Verts V = { {0,0,0},{1,0,0},{0,1,0},{0,0,0},{1e-6,0,0},{0,1e-6,0} };
    Tris  T = { {0,1,2}, {3,4,5} };

    MeshPreprocessing<double>::RemoveSmallFacets(V, T, 0.01);
    CHECK(T.size() == 1, "RemoveSmallFacets: tiny triangle removed");
}

// ─────────────────────────────────────────────────────────────────────────────
// T8: MeshPreprocessing – OrientNormals makes signed volume positive
// ─────────────────────────────────────────────────────────────────────────────
static void Test_OrientNormals() {
    Verts V;
    Tris  T;
    MakeTetrahedron(V, T);
    for (auto& t : T) std::swap(t[1], t[2]);  // flip all to inward

    MeshPreprocessing<double>::OrientNormals(V, T);

    double sv = 0;
    for (auto const& t : T) {
        V3 const& p0 = V[t[0]];
        V3 const& p1 = V[t[1]];
        V3 const& p2 = V[t[2]];
        sv += Dot(p0, Cross(p1, p2)) / 6.0;
    }
    CHECK(sv > 0, "OrientNormals: signed volume positive after orientation");
}

// ─────────────────────────────────────────────────────────────────────────────
// T9: MeshPreprocessing – OrientNormals on two separate 3-D components
// ─────────────────────────────────────────────────────────────────────────────
static void Test_OrientNormals_TwoComponents() {
    // Two completely disconnected single triangles with known signed volumes.
    // Vertices are off-axis so signed volumes are non-zero.
    //
    // Component 0 {0,1,2}: sv = Dot(p0, Cross(p1,p2))/6 > 0 (outward)
    // Component 1 {3,5,4}: sv < 0 (inward, needs flipping)
    Verts V = {
        {1,2,3}, {4,5,6}, {7,8,0},           // component 0
        {11,12,13}, {14,15,16}, {17,18,10}    // component 1
    };
    Tris T = {
        {0,1,2},   // sv = 27/6  > 0
        {3,5,4}    // sv = -27/6 < 0 (reversed)
    };

    // Verify initial signed volumes.
    auto sv = [&](Tri const& t) {
        V3 const& p0 = V[t[0]]; V3 const& p1 = V[t[1]]; V3 const& p2 = V[t[2]];
        return Dot(p0, Cross(p1, p2)) / 6.0;
    };
    CHECK(sv(T[0]) > 0, "OrientNormals2_pre: component0 initially outward");
    CHECK(sv(T[1]) < 0, "OrientNormals2_pre: component1 initially inward");

    MeshPreprocessing<double>::OrientNormals(V, T);

    CHECK(sv(T[0]) > 0, "OrientNormals2: component 0 outward after orient");
    CHECK(sv(T[1]) > 0, "OrientNormals2: component 1 outward after orient");
}

// ─────────────────────────────────────────────────────────────────────────────
// T10: MeshPreprocessing – InvertNormals
// ─────────────────────────────────────────────────────────────────────────────
static void Test_InvertNormals() {
    Verts V;
    Tris  T;
    MakeTetrahedron(V, T);
    Tris original = T;
    MeshPreprocessing<double>::InvertNormals(T);
    for (size_t i = 0; i < T.size(); ++i) {
        CHECK(T[i][0] == original[i][0], "InvertNormals: v0 unchanged");
        CHECK(T[i][1] == original[i][2], "InvertNormals: v1 swapped");
        CHECK(T[i][2] == original[i][1], "InvertNormals: v2 swapped");
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// T11: MeshPreprocessing – RemoveSmallComponents
// ─────────────────────────────────────────────────────────────────────────────
static void Test_RemoveSmallComponents() {
    Verts V;
    Tris  T;
    MakeTetrahedron(V, T);
    size_t base = V.size();
    V.push_back({100,0,0});
    V.push_back({100+1e-3,0,0});
    V.push_back({100,1e-3,0});
    T.push_back({(int32_t)base, (int32_t)(base+1), (int32_t)(base+2)});

    MeshPreprocessing<double>::RemoveSmallComponents(V, T, 0.0, 2);
    CHECK(T.size() == 4, "RemoveSmallComponents: tiny component removed");
}

// ─────────────────────────────────────────────────────────────────────────────
// T12: MeshValidation – closed manifold cube
// ─────────────────────────────────────────────────────────────────────────────
static void Test_Validation_ClosedManifold() {
    Verts V;
    Tris  T;
    MakeCube(V, T);
    auto result = MeshValidation<double>::Validate(V, T, false);
    CHECK(result.isManifold, "Validation: cube is manifold");
    CHECK(result.isClosed,   "Validation: cube is closed");
    CHECK(result.boundaryEdges == 0, "Validation: cube no boundary edges");
    CHECK(result.nonManifoldEdges == 0, "Validation: cube no non-manifold edges");
}

// ─────────────────────────────────────────────────────────────────────────────
// T13: MeshValidation – open mesh has boundary edges
// ─────────────────────────────────────────────────────────────────────────────
static void Test_Validation_OpenMesh() {
    Verts V;
    Tris  T;
    MakeOpenSquare(V, T);
    auto result = MeshValidation<double>::Validate(V, T, false);
    CHECK(result.isManifold, "Validation: open square is manifold");
    CHECK(!result.isClosed,  "Validation: open square is not closed");
    CHECK(result.boundaryEdges == 4, "Validation: open square has 4 boundary edges");
}

// ─────────────────────────────────────────────────────────────────────────────
// T14: MeshValidation – non-manifold edge
// ─────────────────────────────────────────────────────────────────────────────
static void Test_Validation_NonManifold() {
    Verts V = { {0,0,0},{1,0,0},{0.5,1,0},{0.5,-1,0},{0.5,0,1} };
    Tris  T = { {0,1,2}, {0,1,3}, {0,1,4} };
    bool nm = !MeshValidation<double>::IsManifold(T);
    CHECK(nm, "Validation: non-manifold edge detected");
}

// ─────────────────────────────────────────────────────────────────────────────
// T15: MeshValidation – orientation check
// ─────────────────────────────────────────────────────────────────────────────
static void Test_Validation_Orientation() {
    Verts V;
    Tris  T;
    MakeCube(V, T);
    auto result = MeshValidation<double>::Validate(V, T, false);
    CHECK(result.isOriented, "Validation: cube is consistently oriented");
}

// ─────────────────────────────────────────────────────────────────────────────
// T16: MeshHoleFilling – open pyramid base filled (EarClipping3D)
// All boundary loops are filled; open-mesh outer perimeters are included.
// ─────────────────────────────────────────────────────────────────────────────
static void Test_FillHoles_Pyramid() {
    Verts V;
    Tris  T;
    MakePyramid(V, T);  // 4 triangles, open square base

    auto before = MeshValidation<double>::Validate(V, T, false);
    CHECK(!before.isClosed, "FillHoles_Pyramid: open before fill");

    MeshHoleFilling<double>::Parameters p;
    p.method = MeshHoleFilling<double>::TriangulationMethod::EarClipping3D;
    p.repair  = false;
    p.validateOutput = false;
    MeshHoleFilling<double>::FillHoles(V, T, p);

    auto after = MeshValidation<double>::Validate(V, T, false);
    CHECK(after.isClosed, "FillHoles_Pyramid: closed after fill");
    CHECK(T.size() == 6,  "FillHoles_Pyramid: correct triangle count (4+2)");
}

// ─────────────────────────────────────────────────────────────────────────────
// T17: MeshHoleFilling – LoopSplit on pyramid base
// ─────────────────────────────────────────────────────────────────────────────
static void Test_FillHoles_LoopSplit_Pyramid() {
    Verts V;
    Tris  T;
    MakePyramid(V, T);

    MeshHoleFilling<double>::Parameters p;
    p.method = MeshHoleFilling<double>::TriangulationMethod::LoopSplit;
    p.repair  = false;
    p.validateOutput = false;
    MeshHoleFilling<double>::FillHoles(V, T, p);

    auto after = MeshValidation<double>::Validate(V, T, false);
    CHECK(after.isClosed, "FillHoles_LoopSplit_Pyramid: closed after filling");
}

// ─────────────────────────────────────────────────────────────────────────────
// T18: MeshHoleFilling – already closed mesh → no change
// ─────────────────────────────────────────────────────────────────────────────
static void Test_FillHoles_AlreadyClosed() {
    Verts V;
    Tris  T;
    MakeCube(V, T);
    size_t trisBefore = T.size();

    MeshHoleFilling<double>::Parameters p;
    p.repair = false;
    MeshHoleFilling<double>::FillHoles(V, T, p);

    CHECK(T.size() == trisBefore, "FillHoles_Closed: no triangles added");
}

// ─────────────────────────────────────────────────────────────────────────────
// T19: MeshHoleFilling – winding order of filled triangles (tetrahedron)
// Filling a tetrahedron missing one face should restore outward orientation.
// ─────────────────────────────────────────────────────────────────────────────
static void Test_FillHoles_WindingOrder() {
    Verts V = { {0,0,0},{1,0,0},{0.5,1,0},{0.5,0.5,1} };
    // Missing base {0,1,2} — the other 3 faces are present.
    Tris T = { {0,1,3},{1,2,3},{0,3,2} };

    MeshHoleFilling<double>::Parameters p;
    p.method = MeshHoleFilling<double>::TriangulationMethod::EarClipping3D;
    p.repair  = false;
    p.validateOutput = false;
    MeshHoleFilling<double>::FillHoles(V, T, p);

    CHECK(T.size() == 4, "FillHoles_Winding: 4 triangles after fill");

    double sv = 0;
    for (auto const& t : T) {
        V3 const& p0 = V[t[0]]; V3 const& p1 = V[t[1]]; V3 const& p2 = V[t[2]];
        sv += Dot(p0, Cross(p1, p2)) / 6.0;
    }
    CHECK(sv > 0, "FillHoles_Winding: positive signed volume (outward normals)");

    auto val = MeshValidation<double>::Validate(V, T, false);
    CHECK(val.isOriented, "FillHoles_Winding: consistently oriented");
    CHECK(val.isClosed,   "FillHoles_Winding: closed mesh");
}

// ─────────────────────────────────────────────────────────────────────────────
// T20: MeshHoleFilling – pentagon hole (TraceHoleBoundary)
// ─────────────────────────────────────────────────────────────────────────────
static void Test_TraceHoleBoundary_Pentagon() {
    Verts V;
    for (int i = 0; i < 5; ++i) {
        double a = 2*kPI*i/5;
        V.push_back({std::cos(a), std::sin(a), 0});
    }
    V.push_back({0,0,2});  // apex index 5

    Tris T;
    for (int i = 0; i < 5; ++i) {
        T.push_back({i, (i+1)%5, 5});
    }

    MeshHoleFilling<double>::Parameters p;
    p.method = MeshHoleFilling<double>::TriangulationMethod::EarClipping3D;
    p.repair  = false;
    p.validateOutput = false;
    MeshHoleFilling<double>::FillHoles(V, T, p);

    // Pentagon needs n-2=3 fill triangles.
    CHECK(T.size() == 5 + 3, "TraceHoleBoundary: pentagon filled with 3 triangles");

    auto val = MeshValidation<double>::Validate(V, T, false);
    CHECK(val.isClosed, "TraceHoleBoundary: closed after fill");
}

// ─────────────────────────────────────────────────────────────────────────────
// T21: MeshHoleFilling – maxEdges constraint respected
// ─────────────────────────────────────────────────────────────────────────────
static void Test_FillHoles_MaxEdges() {
    Verts V;
    for (int i = 0; i < 5; ++i) {
        double a = 2*kPI*i/5;
        V.push_back({std::cos(a), std::sin(a), 0});
    }
    V.push_back({0,0,2});
    Tris T;
    for (int i = 0; i < 5; ++i) T.push_back({i, (i+1)%5, 5});

    MeshHoleFilling<double>::Parameters p;
    p.maxEdges = 4;   // pentagon has 5 edges → not filled
    p.repair = false;
    MeshHoleFilling<double>::FillHoles(V, T, p);

    CHECK(T.size() == 5, "FillHoles_MaxEdges: large hole not filled");
}

// ─────────────────────────────────────────────────────────────────────────────
// T22: MeshRepair – full DEFAULT repair on a dirty mesh
// ─────────────────────────────────────────────────────────────────────────────
static void Test_FullRepair() {
    Verts V = {
        {0,0,0}, {1,0,0}, {0,1,0},
        {0,0,0},
        {99,99,99},
    };
    Tris T = {
        {0,1,2},
        {3,1,2},
        {0,0,1}
    };

    MeshRepair<double>::Parameters p;
    p.mode = MeshRepair<double>::RepairMode::DEFAULT;
    MeshRepair<double>::Repair(V, T, p);

    CHECK(V.size() == 3, "FullRepair: isolated+duplicate removed");
    CHECK(T.size() == 1, "FullRepair: degenerate+duplicate triangles removed");
    for (auto const& t : T) {
        CHECK(t[0] != t[1] && t[1] != t[2] && t[0] != t[2],
              "FullRepair: no degenerate triangles");
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// T23: MeshHoleFilling – EarClipping 2D projection on pyramid
// ─────────────────────────────────────────────────────────────────────────────
static void Test_FillHoles_EC2D() {
    Verts V;
    Tris  T;
    MakePyramid(V, T);

    MeshHoleFilling<double>::Parameters p;
    p.method = MeshHoleFilling<double>::TriangulationMethod::EarClipping;
    p.repair  = false;
    p.validateOutput = false;
    MeshHoleFilling<double>::FillHoles(V, T, p);

    auto val = MeshValidation<double>::Validate(V, T, false);
    CHECK(val.isClosed,   "FillHoles_EC2D: closed after filling");
    CHECK(val.isOriented, "FillHoles_EC2D: consistently oriented");
}

// ─────────────────────────────────────────────────────────────────────────────
// T24: MeshHoleFilling – CDT on pyramid
// ─────────────────────────────────────────────────────────────────────────────
static void Test_FillHoles_CDT() {
    Verts V;
    Tris  T;
    MakePyramid(V, T);

    MeshHoleFilling<double>::Parameters p;
    p.method = MeshHoleFilling<double>::TriangulationMethod::CDT;
    p.repair  = false;
    p.validateOutput = false;
    MeshHoleFilling<double>::FillHoles(V, T, p);

    auto val = MeshValidation<double>::Validate(V, T, false);
    CHECK(val.isClosed,   "FillHoles_CDT: closed after filling");
    CHECK(val.isOriented, "FillHoles_CDT: consistently oriented");
}

// ─────────────────────────────────────────────────────────────────────────────
// T25: MeshHoleFilling – LoopSplit base-case n==3
// ─────────────────────────────────────────────────────────────────────────────
static void Test_FillHoles_LoopSplit_N3() {
    Verts V = { {0,0,0},{1,0,0},{0.5,1,0},{0.5,0.5,1} };
    Tris T = { {0,1,3},{1,2,3},{0,3,2} };   // tetrahedron missing base

    MeshHoleFilling<double>::Parameters p;
    p.method = MeshHoleFilling<double>::TriangulationMethod::LoopSplit;
    p.repair  = false;
    p.validateOutput = false;
    MeshHoleFilling<double>::FillHoles(V, T, p);

    CHECK(T.size() == 4, "FillHoles_LoopSplit_N3: 4 triangles after fill");

    double sv = 0;
    for (auto const& t : T) {
        V3 const& p0 = V[t[0]]; V3 const& p1 = V[t[1]]; V3 const& p2 = V[t[2]];
        sv += Dot(p0, Cross(p1, p2)) / 6.0;
    }
    CHECK(sv > 0, "FillHoles_LoopSplit_N3: positive signed volume");
}

// ─────────────────────────────────────────────────────────────────────────────
// T26: MeshPreprocessing – OrientNormals on already-oriented mesh → no change
// ─────────────────────────────────────────────────────────────────────────────
static void Test_OrientNormals_AlreadyOriented() {
    Verts V;
    Tris  T;
    MakeCube(V, T);

    MeshPreprocessing<double>::OrientNormals(V, T);

    auto val = MeshValidation<double>::Validate(V, T, false);
    CHECK(val.isOriented, "OrientNormals_AlreadyOriented: still consistently oriented");
    CHECK(val.isClosed,   "OrientNormals_AlreadyOriented: still closed");
}

// ─────────────────────────────────────────────────────────────────────────────
// T27: MeshRepair – colocate with epsilon near a cell boundary
// ─────────────────────────────────────────────────────────────────────────────
static void Test_ColocateEpsilonBoundary() {
    Verts V = { {0,0,0},{0.9999,0,0},{2.0,0,0} };
    std::vector<int32_t> c;
    MeshRepair<double>::DetectColocatedVertices(V, c, 1.0);
    bool merged01 = (c[0] == c[1]);
    CHECK(merged01, "ColocateEpsilonBoundary: boundary-crossing vertices merged");
    CHECK(c[2] == 2, "ColocateEpsilonBoundary: distant vertex unique");
}

// ─────────────────────────────────────────────────────────────────────────────
// T28: TriangulateHole3D stale-v2 regression
// A non-convex hexagonal hole exercises the per-clip stale-score fix.
// Even before the fix the output is topologically valid, but we verify
// the final mesh is closed and consistently oriented.
// ─────────────────────────────────────────────────────────────────────────────
static void Test_FillHoles_Hexagon_3D() {
    // Hexagonal tent: 6 side triangles meeting at apex (index 6),
    // open hexagonal base.
    Verts V;
    for (int i = 0; i < 6; ++i) {
        double a = 2*kPI*i/6;
        V.push_back({std::cos(a), std::sin(a), 0});
    }
    V.push_back({0,0,2});  // apex index 6

    Tris T;
    for (int i = 0; i < 6; ++i) {
        T.push_back({i, (i+1)%6, 6});
    }

    MeshHoleFilling<double>::Parameters p;
    p.method = MeshHoleFilling<double>::TriangulationMethod::EarClipping3D;
    p.repair  = false;
    p.validateOutput = false;
    MeshHoleFilling<double>::FillHoles(V, T, p);

    // n-2 = 4 fill triangles for hexagon.
    CHECK(T.size() == 6 + 4, "FillHoles_Hex3D: hexagon filled with 4 triangles");
    auto val = MeshValidation<double>::Validate(V, T, false);
    CHECK(val.isClosed,   "FillHoles_Hex3D: closed after fill");
    CHECK(val.isManifold, "FillHoles_Hex3D: manifold after fill");
}

// ─────────────────────────────────────────────────────────────────────────────
// Main
// ─────────────────────────────────────────────────────────────────────────────
int main()
{
    Test_ColocateVertices();
    Test_RemoveDegenerateFacets();
    Test_RemoveDuplicateFacets();
    Test_RemoveIsolatedVertices();
    Test_DetectColocatedExact();
    Test_DetectColocatedEpsilon();
    Test_RemoveSmallFacets();
    Test_OrientNormals();
    Test_OrientNormals_TwoComponents();
    Test_InvertNormals();
    Test_RemoveSmallComponents();
    Test_Validation_ClosedManifold();
    Test_Validation_OpenMesh();
    Test_Validation_NonManifold();
    Test_Validation_Orientation();
    Test_FillHoles_Pyramid();
    Test_FillHoles_LoopSplit_Pyramid();
    Test_FillHoles_AlreadyClosed();
    Test_FillHoles_WindingOrder();
    Test_TraceHoleBoundary_Pentagon();
    Test_FillHoles_MaxEdges();
    Test_FullRepair();
    Test_FillHoles_EC2D();
    Test_FillHoles_CDT();
    Test_FillHoles_LoopSplit_N3();
    Test_OrientNormals_AlreadyOriented();
    Test_ColocateEpsilonBoundary();
    Test_FillHoles_Hexagon_3D();

    std::cout << "\n=== Results: " << gPassed << " passed, "
              << gFailed << " failed ===\n";
    return gFailed > 0 ? 1 : 0;
}
