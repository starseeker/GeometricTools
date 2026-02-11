BRL-CAD is currently making use of a number of algorithms from Geogram.  Geogram
as a dependency is a bit tricky to deal with from a portability perspective, so
we would like to replace our use of Geogram with GTE features instead.  However,
it is known that GTE does not have full feature parity with what we are using in
Geogram.  What we would like to do is implement the missing pieces of Geogram's
capabilities in the GTE framework, using GTE's exciting capabilities whenever
possible and "filling in the blanks" by translating Geogram's implementations
over to GTE style code when needed.

Geogram and GTE are both license compatible as far as BRL-CAD is concerned, so
there is no issue with translating Geogram code into GTE style and types and
reworking it - we don't need a "clean-room" reimplementation.  We do want to
end up with code in the GTE style (for example, we don't want to use geogram's
option-via-string settings system and should provide any necessary feature or
method knobs using GTE style option handling.  Basically, we want the core geogram
algorithms in GTE style code.

We also want to embrace GTE's "header-only" approach to implementation - geogram
compiles into libraries, whereas GTE has us include headers and has no platform
specific code - we want to embrace the GTE approach for the new code.

The top level file gt.obj contains a significant number of meshes, some of which
are non-manifold and are repairable using the geogram-based approach seen in the
repair.cpp code in the brlcad_user_code examples.  You'll need to construct stand-alone
tests that replicate the steps using the gt.obj file as an input, since the full
BRL-CAD compile is too slow for these purposes, but you'll want to verify that
any repairs that successfully produce manifold inputs using geogram also do so
with the new GTE code (if the GTE code produces better results than geogram that's
fine, but we definitely don't want it to be worse.) Similarly, you'll want to
verify the same or better results for the other Geogram algorithms migrated into
GTE's framework and style.

Logic from geogram recast into GTE style should be in separate files and use the
geogram license, but otherwise match GTE's code formatting and commenting styles.

Success is defined as producing new GTE-style "add-on" header implementations of
the geogram functionality relied on by the brlcad_user_code examples - we want to
be able to drop the new GTE style headers into an existing GTE install, update
our BRL-CAD code to replace the Geogram code completely, and retain the same or
better processing capabilities.  This will allow BRL-CAD to eliminate geogram as
a dependency.

For Screened Poisson Surface Reconstruction (SPSR), you may directly use the
header form of the upstream code the way the bg_3d_spsr implementation does
(see brlcad_spsr/spsr.cpp) - there is no need to reimplement that in GTE terms
(if GTE has its own implementation present that is robust enough you can use
that, but otherwise lets fall back on the upstream.)  If (and only if) neither
GTE nor the upstream SPSR are able to support what is needed, you may migrate
geogram's version of SPSR logic to the GTE style as well (be sure to maintain
any license/copyrights when you do so, and try to keep any logic with separate
copyright and license info in separate headers)

Assuming this ends up needing to be done in stages, document the current state
in the repository to allow new AI sessions to continue with the work from the
last intermediate state.

Here is the initial plan that an AI discussion came up with:

Phase 1: Port Geogram Mesh Repair

Create GTE/Mathematics/MeshRepair.h by porting Geogram's mesh repair logic:
C++

// GTE/Mathematics/MeshRepair.h
// Ported from Geogram mesh_repair.cpp

namespace gte {

template <typename Real>
class MeshRepair {
public:
    enum class RepairMode {
        DEFAULT = 0,
        COLOCATE = 1,           // Merge vertices at same location
        DUPLICATE_VERTICES = 2,  // Remove duplicate vertices
        DEGENERATE_FACES = 4,    // Remove degenerate triangles
        ISOLATED_VERTICES = 8    // Remove unused vertices
    };
    
    struct Parameters {
        Real epsilon;           // Tolerance for vertex merging
        RepairMode mode;
        Real maxHoleArea;       // For hole filling
        bool triangulate;       // Ensure output is triangles
        
        Parameters() 
            : epsilon(1e-6)
            , mode(RepairMode::DEFAULT)
            , maxHoleArea(0.0)
            , triangulate(true) 
        {}
    };
    
    // Main repair function (ported from Geogram::mesh_repair)
    static void Repair(
        std::vector<Vector3<Real>>& vertices,
        std::vector<std::array<int32_t, 3>>& triangles,
        Parameters const& params);
        
private:
    // Ported from Geogram's mesh_preprocessing.cpp
    static void ColocateVertices(/* ... */);
    static void RemoveDuplicateVertices(/* ... */);
    static void RemoveDegenerateFacets(/* ... */);
    static void RemoveIsolatedVertices(/* ... */);
    
    // Supporting structures (adapt from Geogram)
    struct SpatialSort {
        // Port Geogram's spatial sorting for vertex proximity
    };
};

} // namespace gte

Port Strategy for mesh_repair():

From Geogram's mesh_preprocessing.cpp:
C++

// Source: geogram/mesh/mesh_repair.cpp (lines ~180-350)
// Port the core logic:

void mesh_repair(
    Mesh& M,
    MeshRepairMode mode,
    double epsilon
) {
    // 1. Spatial sort vertices
    // 2. Merge close vertices
    // 3. Remove degenerate facets
    // 4. Compact vertex/facet arrays
}

Port to GTE:
C++

template <typename Real>
void MeshRepair<Real>::Repair(
    std::vector<Vector3<Real>>& vertices,
    std::vector<std::array<int32_t, 3>>& triangles,
    Parameters const& params)
{
    // Use GTE's existing VETManifoldMesh for topology
    VETManifoldMesh mesh;
    
    // Port Geogram's spatial sorting logic
    if (params.mode & RepairMode::COLOCATE) {
        ColocateVertices(vertices, triangles, params.epsilon);
    }
    
    // Port Geogram's degeneracy removal
    if (params.mode & RepairMode::DEGENERATE_FACES) {
        RemoveDegenerateFacets(vertices, triangles, params.epsilon);
    }
    
    // ... etc
}

Phase 2: Port Hole Filling

Port Geogram's mesh_fill_holes.cpp (~400 lines):
C++

// GTE/Mathematics/MeshHoleFilling.h
// Ported from Geogram mesh_fill_holes.cpp

namespace gte {

template <typename Real>
class MeshHoleFilling {
public:
    // Ported from Geogram::fill_holes()
    static void FillHoles(
        std::vector<Vector3<Real>>& vertices,
        std::vector<std::array<int32_t, 3>>& triangles,
        Real maxHoleArea);
        
private:
    // Port from Geogram's hole detection
    struct HoleBoundary {
        std::vector<int32_t> vertices;
        Real area;
    };
    
    static std::vector<HoleBoundary> DetectHoles(
        ETManifoldMesh const& mesh);
    
    // Port Geogram's hole triangulation strategies
    // (it uses multiple algorithms: ear clipping, advancing front, etc.)
    static void TriangulateHole(
        HoleBoundary const& hole,
        std::vector<Vector3<Real>> const& vertices,
        std::vector<std::array<int32_t, 3>>& newTriangles);
};

} // namespace gte

Key Geogram code to port (from mesh_fill_holes.cpp):

    Boundary loop detection (~100 lines)
    Hole area computation (~50 lines)
    Advancing front triangulation (~200 lines)

Phase 3: Port Mesh Preprocessing Utilities (3-5 days)

Port supporting functions from mesh_preprocessing.cpp:
C++

// GTE/Mathematics/MeshPreprocessing.h

namespace gte {

template <typename Real>
class MeshPreprocessing {
public:
    // Ported from Geogram::remove_small_facets
    static void RemoveSmallFacets(
        std::vector<Vector3<Real>>& vertices,
        std::vector<std::array<int32_t, 3>>& triangles,
        Real minArea);
    
    // Ported from Geogram::remove_small_connected_components
    static void RemoveSmallComponents(
        std::vector<Vector3<Real>>& vertices,
        std::vector<std::array<int32_t, 3>>& triangles,
        Real minArea,
        size_t minFacets);
    
    // Ported from Geogram::orient_normals
    static void OrientNormals(
        std::vector<Vector3<Real>> const& vertices,
        std::vector<std::array<int32_t, 3>>& triangles);
        
private:
    // Use GTE's existing Mesh.h for normal computation
    static Vector3<Real> ComputeFacetNormal(/* ... */);
    
    // Port Geogram's connected component traversal
    static void GetConnectedComponents(/* ... */);
};

} // namespace gte

Phase 4: Remeshing Strategy Decision

For remesh.cpp, we have three options:
Option A: Port Geogram's CVT

Port the CVT (Centroidal Voronoi Tessellation) algorithm from Geogram:
C++

// GTE/Mathematics/CVTRemesh.h
// Ported from Geogram voronoi/CVT.cpp

namespace gte {

template <typename Real>
class CVTRemesher {
public:
    // Port Geogram::remesh_smooth()
    static void RemeshSmooth(
        std::vector<Vector3<Real>> const& inputVertices,
        std::vector<std::array<int32_t, 3>> const& inputTriangles,
        std::vector<Vector3<Real>>& outputVertices,
        std::vector<std::array<int32_t, 3>>& outputTriangles,
        size_t targetPointCount);
        
private:
    // Port Geogram's Lloyd iterations
    void LloydRelaxation(/* ... */);
    
    // Port Geogram's Newton solver for CVT
    void NewtonOptimization(/* ... */);
    
    // Use GTE's Delaunay3 for RDT (Restricted Delaunay)
    void ComputeRestrictedDelaunay(/* ... */);
};

} // namespace gte

Effort: ~400 lines from Geogram's CVT.cpp + ~300 lines from mesh_remesh.cpp
Option B: Use GTE's VertexCollapseMesh + Subdivision (1 week)

Simpler approach without CVT:
C++

// GTE/Mathematics/SimpleRemesh.h

namespace gte {

template <typename Real>
class SimpleRemesher {
public:
    // Iterative decimation + subdivision approach
    static void Remesh(
        std::vector<Vector3<Real>>& vertices,
        std::vector<std::array<int32_t, 3>>& triangles,
        Real targetEdgeLength);
        
private:
    // Use GTE's existing VertexCollapseMesh
    static void Decimate(/* ... */);
    
    // Implement simple edge split
    static void Subdivide(/* ... */);
    
    // Smooth vertices (Laplacian or similar)
    static void Smooth(/* ... */);
};

} // namespace gte

Recommendation: Start with Option B for quick migration, add Option A after basic capability is in place.

Phase 5:  Co3Ne - translate to GTE.  (Initial plan skipped this, but we do want it as an alternative
methodology to experiment with so we do want it ported.)

Phase 6: Produce proposed BRL-CAD file updates (won't be able to test here, but produce to make
eventual testing simpler in main BRL-CAD tree)

Update BRL-CAD files to use new GTE implementations:
C++

// src/librt/primitives/bot/repair_gte.cpp

#include "GTE/Mathematics/MeshRepair.h"
#include "GTE/Mathematics/MeshHoleFilling.h"
#include "GTE/Mathematics/MeshPreprocessing.h"

int rt_bot_repair(
    struct rt_bot_internal **obot,
    struct rt_bot_internal *bot,
    struct rt_bot_repair_info *settings)
{
    // Convert to GTE format
    std::vector<gte::Vector3<double>> vertices;
    std::vector<std::array<int32_t, 3>> triangles;
    bot_to_gte(bot, vertices, triangles);
    
    // Configure repair parameters
    gte::MeshRepair<double>::Parameters params;
    params.epsilon = 1e-6 * bbox_diagonal;
    params.mode = gte::MeshRepair<double>::RepairMode::DEFAULT;
    params.maxHoleArea = settings->max_hole_area;
    
    // Step 1: Repair mesh topology
    gte::MeshRepair<double>::Repair(vertices, triangles, params);
    
    // Step 2: Fill holes
    gte::MeshHoleFilling<double>::FillHoles(
        vertices, triangles, params.maxHoleArea);
    
    // Step 3: Clean up small components
    double min_area = 0.03 * total_area;  // Port Geogram's heuristic
    gte::MeshPreprocessing<double>::RemoveSmallComponents(
        vertices, triangles, min_area, 0);
    
    // Convert back to rt_bot_internal
    *obot = gte_to_bot(vertices, triangles);
    
    // Validate with Manifold (keep existing check)
    if (settings->strict) {
        manifold::MeshGL64 check_mesh = bot_to_manifold(*obot);
        manifold::Manifold mf(check_mesh);
        if (mf.Status() != manifold::Manifold::Error::NoError) {
            return -1;
        }
    }
    
    return 0;
}


Code Porting Checklist

For each Geogram function being ported:
Code

[ ] Identify Geogram source file and line range
[ ] Extract core algorithm (remove Geogram-specific dependencies)
[ ] Adapt to GTE conventions:
    - Use gte::Vector3<Real> instead of GEO::vec3
    - Use std::vector instead of GEO::vector
    - Use GTE's mesh structures (ETManifoldMesh, VETManifoldMesh)
[ ] Add unit tests comparing output to Geogram
[ ] Document original Geogram source location in comments

Example:
C++

// GTE/Mathematics/MeshRepair.cpp
// Ported from Geogram src/lib/geogram/mesh/mesh_repair.cpp
// Lines 245-380: vertex colocate function
// License: BSD (compatible)

template <typename Real>
void MeshRepair<Real>::ColocateVertices(
    std::vector<Vector3<Real>>& vertices,
    std::vector<std::array<int32_t, 3>>& triangles,
    Real epsilon)
{
    // Original Geogram implementation:
    // https://github.com/BrunoLevy/geogram/blob/main/src/lib/geogram/mesh/mesh_repair.cpp#L245
    
    // [Ported code here]
}
File Structure
Code

GTE/Mathematics/
├── MeshRepair.h              # NEW - Ported from mesh_repair.cpp
├── MeshRepair.cpp            # 
├── MeshHoleFilling.h         # NEW - Ported from mesh_fill_holes.cpp
├── MeshHoleFilling.cpp       #
├── MeshPreprocessing.h       # NEW - Ported from mesh_preprocessing.cpp
├── MeshPreprocessing.cpp     #
├── SimpleRemesher.h          # NEW - GTE-native simple approach
├── SimpleRemesher.cpp        #
└── CVTRemesher.h             # NEW - Ported from CVT.cpp
    CVTRemesher.cpp           #

(Keep existing GTE files)
├── ETManifoldMesh.h          # Use for topology
├── VETManifoldMesh.h         # Use for mesh graph
├── VertexCollapseMesh.h      # Use for decimation
└── Delaunay3.h               # Use for triangulation

Testing Strategy

Create comparative tests against Geogram:
C++

// tests/gte_mesh_repair_test.cpp

TEST(MeshRepair, VertexColocate) {
    // Load test mesh with known duplicates
    auto [verts, tris] = load_test_mesh("swiss_cheese.obj");
    
    // Test GTE version
    gte::MeshRepair<double>::Parameters params;
    params.epsilon = 1e-6;
    gte::MeshRepair<double>::Repair(verts, tris, params);
    
    // Compare vertex count, topology
    EXPECT_EQ(verts.size(), expected_vertex_count);
    EXPECT_TRUE(is_manifold(verts, tris));
}

TEST(HoleFilling, SmallHoles) {
    auto [verts, tris] = load_mesh_with_holes("holey_mesh.obj");
    
    gte::MeshHoleFilling<double>::FillHoles(verts, tris, 0.01);
    
    EXPECT_EQ(count_boundary_edges(verts, tris), 0);
}

Risk Mitigation
Risk	Mitigation
Porting introduces bugs	Side-by-side testing with Geogram on same inputs
GTE doesn't have needed data structures	Use GTE's existing manifold meshes + adapt
Performance regression	Profile and optimize hot paths
License documentation	Clear attribution in headers
Documentation Requirements

Each ported file needs:
C++

/*
 * Ported from Geogram: https://github.com/BrunoLevy/geogram
 * Original file: src/lib/geogram/mesh/mesh_repair.cpp
 * Original license: BSD 3-Clause (see COPYING)
 * 
 * Adaptations for GeometricTools:
 * - Changed from GEO::Mesh to std::vector<Vector3<Real>>
 * - Uses GTE::ETManifoldMesh for topology
 * - Removed Geogram-specific Logger calls
 */


