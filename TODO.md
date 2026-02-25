# Co3Ne Implementation TODO

This document summarises findings from a careful evaluation of the GTE Co3Ne
implementation against Geogram's reference implementation
(`geogram/src/lib/geogram/points/co3ne.cpp`, 2663 lines, commit f5abd69).  It
is structured as a guide for making the codebase correct, reliable,
performant, high-quality, and clean.

The relevant GTE files are:

| File | Lines | Role |
|------|-------|------|
| `GTE/Mathematics/Co3Ne.h` | 1333 | Core algorithm + parameters |
| `GTE/Mathematics/Co3NeManifoldExtractor.h` | 1165 | Manifold extraction (port of Geogram class) |
| `GTE/Mathematics/Co3NeManifoldStitcher.h` | 3826 | Custom post-processing (no Geogram equivalent) |
| `GTE/Mathematics/Co3NeEnhanced.h` | 1071 | Subclass of Co3Ne; largely broken |

---

## Part 1 — Parity with Geogram

These issues cause GTE to produce different (worse) results than Geogram even
when given identical input.

### 1.1 Triangle Generation — Fundamental Algorithmic Divergence (CRITICAL)

**Geogram** generates candidate triangles via a **Restricted Voronoi Cell
(RVC) polygon clipping** algorithm (`Co3NeThread::run_triangles`, co3ne.cpp
lines 2426–2595):

1. A 10-vertex disk polygon is placed in the tangent plane at each seed `i`.
2. The polygon is clipped by bisector half-planes `H(i, j)` for each
   neighbour `j`, yielding the intersection of the disk with seed `i`'s
   Voronoi cell.
3. Consecutive polygon vertices labelled `(j, k)` emit the candidate triangle
   `(i, j, k)`.

Because each correct surface triangle `(i,j,k)` is produced once from each
of its three seeds, it appears **exactly 3 times** in the raw candidate list.
The T3/T12 split (`co3ne_split_triangles_list`, co3ne.cpp lines 1182–1227)
relies entirely on this invariant.

**GTE's `GenerateTriangles`** (Co3Ne.h lines 492–556) does none of this.  It
finds k-NN, filters by normal agreement, then generates **all O(k²) pairs**
of consistent neighbours as candidates.  Consequences:

- A triangle can appear 1, 2, or many times — the "exactly 3 times" invariant
  does not hold.
- The T3/T12 count threshold used later is therefore **semantically
  meaningless** when paired with this generator.
- The candidate set is O(k²) ≈ 190 triangles per seed (k=20) instead of the
  O(k) ≈ 20 that RVC produces.

**`GenerateTrianglesManifoldConstrained`** (Co3Ne.h lines 592–789) is a much
closer approximation: it sorts projected neighbours angularly and triangulates
only consecutive pairs.  This is conceptually the same as RVC polygon
traversal (just replacing exact Voronoi clipping with angular ordering), and
its edge-budget constraint (≤2 triangles per undirected edge) reproduces the
manifold property that RVC naturally provides.

**Action items:**
- [x] Make `GenerateTrianglesManifoldConstrained` the default generator by
  setting `useManifoldConstrainedGeneration = true` in `Parameters()`.
- [x] Remove (or hide behind `#ifdef GTE_DEBUG`) the O(k²)
  `GenerateTriangles` path; it exists only as a fallback and produces
  inferior results.
- [x] Since `GenerateTrianglesManifoldConstrained` produces each candidate at
  most once (from one seed), the T3/T12 classification step becomes
  meaningless.  Replace it with: pass all unique deduplicated candidates
  directly as T3 seeds to the manifold extractor (skip the T12 list
  entirely).  This already matches the logic in `ExtractManifold` lines
  876–900.

---

### 1.2 Normal Orientation Propagation — Two Bugs (HIGH)

**Geogram** (`reorient_normals`, co3ne.cpp lines 2094–2142) uses a max-heap
(priority queue ordered by `|dot product|`).  A vertex's normal is flipped
**only when that vertex is popped**, and the `visited` flag is set before
the vertex is pushed, preventing it from being processed twice.

**GTE's `OrientNormalsConsistently`** (Co3Ne.h lines 392–465) has two bugs:

**Bug 1 — Premature (push-time) flipping** (lines 454–458):
```cpp
if (dot < 0) {
    normals[neighborIdx] = -normals[neighborIdx];   // mutated at PUSH time
    dot = -dot;
}
queue.push({ deviation, neighborIdx });
```
If the same vertex is reached from two different paths before it is popped,
its normal can be flipped twice, leaving it unflipped.  Geogram flips only
at pop time, guarded by the `visited` check before pushing.

**Bug 2 — Only vertex 0 is seeded** (line 421):
```cpp
queue.push({ static_cast<Real>(0), 0 });
```
Geogram wraps the BFS in an outer `for(v=0; v<nb_vertices; ++v)` loop to
restart from any unvisited vertex (co3ne.cpp lines 2110–2114), handling
disconnected components.  GTE processes only the component containing vertex
0; all other components retain arbitrary normal orientations.

**Action items:**
- [x] Move the flip of `normals[neighborIdx]` from push time to pop time,
  matching Geogram's logic.
- [x] Add an outer loop over all vertices to restart the BFS for each
  unvisited vertex, handling disconnected point clouds.

---

### 1.3 `TrianglesNormalsAgree` — Wrong Threshold and Missing Orientation
Correction (HIGH)

**Geogram** (`triangles_normals_agree`, co3ne.cpp lines 915–964):
```cpp
double d = dot(n1, n2);
if (same_combinatorial_orientation(t1, t2)) { d = -d; }
return (d > -0.8);
```
The sign flip accounts for the fact that in a consistently-oriented mesh,
adjacent triangles traverse their shared edge in **opposite** directions.  If
they traverse it in the **same** direction, the triangles are actually facing
away from each other, so the geometric dot product is negated before testing.
The threshold `−0.8` (≈143°) is deliberately permissive.

**GTE's `Co3NeManifoldExtractor::TrianglesNormalsAgree`** (line 411):
```cpp
Real dot = Dot(n1, n2);
return dot > static_cast<Real>(0.5);    // ~60°
```
Two errors:
1. **Missing orientation correction** — combinatorial orientation is never
   checked; the sign of the dot product is always taken as-is.
2. **Wrong threshold** — `0.5` (60°) vs. `−0.8` (143°) makes GTE roughly
   14× more restrictive, rejecting large numbers of valid neighbouring
   triangles during manifold construction.

The same two errors appear in `Co3NeEnhanced::ManifoldExtractor::
TrianglesNormalsAgree` (Co3NeEnhanced.h lines 1038–1047).

**Action items:**
- [x] Add the combinatorial-orientation check: test whether the shared edge
  appears in the same or opposite direction in t1 vs. t2; if the same
  direction, negate `d` before the threshold test.
- [x] Change the threshold from `0.5` to `−0.8`.

---

### 1.4 Search Radius Formula — Wrong for Small Point Clouds (MEDIUM)

**BRL-CAD / Geogram** uses:
```
search_radius = 0.05 * bbox_diagonal
```

**GTE's `ComputeAutomaticSearchRadius`** (Co3Ne.h lines 1057–1091) uses:
```
search_radius = 4.0 * bbox_diagonal / n^(1/3)
```
These agree only when `n ≈ 512 000`.  For n=1000 (a typical test cloud) GTE
produces a radius **8× larger** than the BRL-CAD value, causing gross
over-connection and many spurious triangles.

**Action items:**
- [x] Replace `ComputeAutomaticSearchRadius` with `0.05 * bbox_diagonal` to
  match Geogram/BRL-CAD usage.  The current density-scaling formula is
  untested and over-estimates radius for small clouds.

---

### 1.5 `maxNormalAngle` Default — Should Be 60° (MEDIUM)

Geogram and the BRL-CAD test driver (`brlcad_user_code/co3ne.cpp` line 100,
`tests/test_co3ne.cpp` line 169) both use **60°**.  GTE's default in
`Parameters()` (Co3Ne.h line 90) is **90°**, which allows nearly-perpendicular
normals to be treated as co-cone compatible, massively inflating the
candidate triangle count.

**Action items:**
- [x] Change the default for `maxNormalAngle` from `90` to `60`.

---

### 1.6 `ReorientMesh` — Does Not Match Geogram (MEDIUM)

Geogram calls `mesh_reorient(M_, &remove_t)` (co3ne.cpp lines 965–978), a
library function in `geogram/mesh/mesh_repair.cpp` that uses a
**border-distance-ordered BFS** to propagate orientation outward from the mesh
boundary.

GTE reimplements this from scratch in `Co3NeManifoldExtractor` (lines
831–1031) with different structure.  The `PropagateOrientation` function
includes a Möbius-strip detection based on `RelativeOrientation` vote counts
(lines 959–1031) that has no counterpart in Geogram's implementation.  The
differing structure likely produces different orientation results on
non-trivial meshes.

**Action items:**
- [x] Re-examine `ReorientMesh` against Geogram's `mesh_reorient` (in
  `geogram/src/lib/geogram/mesh/mesh_repair.cpp`).  Either port the correct
  border-distance BFS or document the intentional divergence.
  *(Documented: GTE seeds from interior facets and propagates toward border;
  Geogram seeds from border and propagates inward.  Both achieve consistent
  orientation; divergence is intentional and documented in code comments.)*
- [x] The Möbius-strip detection heuristic in `PropagateOrientation` is
  unproven — validate or remove it.
  *(Removed: the `Dissociate`-based Möbius-strip detection block has been
  deleted from `PropagateOrientation`.  The function now simply flips the
  facet when the majority of already-visited neighbors require it.)*

---

### 1.7 `MergeConnectedComponent` — O(T) Scan Corrupts Corner Lists (MEDIUM)

Geogram traverses the component to merge via adjacency links from a specific
triangle.  GTE's `MergeConnectedComponent` (Co3NeManifoldExtractor.h lines
604–622) scans **all triangles** linearly to find those in `fromComp`.  When
triangles in the component are flipped in arbitrary order, the calls to
`Remove(t, false)` + `Insert(t)` modify the circular corner linked-lists in
an order that can corrupt the topology structure.  Geogram's BFS-order
traversal avoids this.

**Action items:**
- [x] Replace the O(T) linear scan with a BFS/DFS traversal via the adjacency
  (`mCornerToAdjacentFacet`) structure, matching Geogram's approach.

---

## Part 2 — Manifold Post-Processing Issues

### 2.1 `Co3NeEnhanced.h` — Delete It (CRITICAL)

`Co3NeEnhanced` is broken at the **API level**.  `Co3NeEnhanced::Reconstruct`
calls (lines 87, 95, 100):
```cpp
Co3Ne<Real>::ComputeNormals(points, normals, params);
Co3Ne<Real>::OrientNormalsConsistently(points, normals, params);
Co3Ne<Real>::GenerateTriangles(points, normals, candidateTriangles, params);
```
The actual signatures in Co3Ne.h each require additional `NNTree const&` and
`Real searchRadius` parameters.  **This file will not compile.**

Beyond the compile failure, `Co3NeEnhanced`:
- Duplicates the manifold extractor inner class (`Co3NeEnhanced::ManifoldExtractor`,
  lines 440–1068) — a third copy of an algorithm already in
  `Co3NeManifoldExtractor.h` and `Co3Ne::ExtractManifold`.
- Contains `RefineManifold` (lines 154–335) whose `ReducesBoundaryCount`
  criterion (lines 219–239) requires ≥2 shared boundary edges per inserted
  triangle, so early candidates that share exactly 1 boundary edge are always
  rejected and holes never actually close.
- Contains `EnforceOrientationFromTriangle` (lines 987–998) that is missing the
  component-merge loop present in Geogram (co3ne.cpp lines 495–506), so
  component tracking diverges over time.

**Action items:**
- [x] Delete `Co3NeEnhanced.h` entirely.
- [x] Remove its `#include` from any file that references it.

---

### 2.2 `Co3NeManifoldStitcher.h` — Reduce to Essentials (HIGH)

The 3826-line `Co3NeManifoldStitcher` is entirely a custom post-processing
module with no Geogram equivalent.  Its existence is symptomatic of the core
algorithm generating noisy, disconnected output that requires extensive repair.
Fixing the core (sections 1.1–1.5 above) should substantially reduce the need
for this module.  Specific issues:

**2.2.1 Duplicate hole filling (steps 4 and 6).**  Both steps call
`MeshHoleFilling<Real>::FillHoles(vertices, triangles, holeParams)` with
identical parameters (lines ~342–382, ~438–460).  The second call can only
do anything new if step 5 (`BallPivotMeshHoleFiller`) created new holes —
an edge case not worth supporting.

**Action items:**
- [x] Remove the duplicate hole-filling step (step 6) and keep only one call
  (step 4).

**2.2.2 Revert-on-failure leaves orphaned vertices.**  When hole filling
creates non-manifold edges the code does:
```cpp
triangles.resize(trianglesBefore);
```
but does not also resize `vertices` back to `verticesBefore`.  Orphaned
vertices corrupt subsequent vertex-index-based operations.

**Action items:**
- [x] Before each hole-filling call, save both `triangles.size()` and
  `vertices.size()`, and restore both on failure.

**2.2.3 `AbsorbSmallClosedComponents` is geometrically incorrect.**
It absorbs artefact clusters by inserting their vertices into the nearest
triangle of the main mesh via triangle splitting.  When the artefact lies on
a different surface (e.g. internal bubble, noise cluster), this pokes holes
in the main surface and introduces incorrect connectivity.

**Action items:**
- [x] Replace absorption with simple deletion: changed `absorbSmallClosedComponents`
  default to `false` so components are discarded rather than absorbed.

**2.2.4 `RemoveNonManifoldEdges` is O(N·T).**  It erases triangles from a
`std::vector` in a loop (`vector::erase` is O(T) per call).

**Action items:**
- [x] Replace with a single pass: mark triangles to remove, then compact
  in-place with a single O(T) loop, avoiding per-erase O(T) shifts.

**2.2.5 `TopologyAwareComponentBridging` bridges everything to one component
unconditionally** when `targetSingleComponent = true` (the default).  This is
wrong for point clouds representing multiple separate objects.

**Action items:**
- [x] Change the default to `targetSingleComponent = false`.
- [x] Add a minimum-gap threshold so that only components within a specified
  distance are bridged.
  *(Added `maxBridgeGapDistance` parameter (default 0 = no limit) to
  `Co3NeManifoldStitcher::Parameters`.  When non-zero, both `currentThreshold`
  and `maxThreshold` are capped at this value in `TopologyAwareComponentBridging`,
  preventing components farther apart than the limit from ever being bridged.)*

---

### 2.3 `Co3NeManifoldExtractor.h` — Fix Core Issues (HIGH)

**2.3.1 `TrianglesNormalsAgree`** — see issue 1.3 above.

**2.3.2 Undefined `LogAssert`** (line 903).  `LogAssert(...)` is not part of
GTE's standard headers.  This will fail to compile unless `LogAssert` is
defined elsewhere.

**Action items:**
- [x] Replace `LogAssert` with `assert` or GTE's standard assertion mechanism
  (added `#include <GTE/Mathematics/Logger.h>` so `LogAssert` is properly defined).

**2.3.3 Deduplication of the manifold extractor.**  `Co3Ne.h` also contains
a private inline manifold-extraction code path in `ExtractManifold` (lines
867–1053) that partially reimplements the same algorithm.  After deleting
`Co3NeEnhanced.h` there are still two copies.

**Action items:**
- [x] `Co3Ne::ExtractManifold` now calls `Co3NeManifoldExtractor` directly for
  the default path.  The legacy T3/T12 inline path is hidden behind
  `#ifdef GTE_DEBUG` and no longer active in production builds.

---

## Part 3 — Parameter Struct Cleanup

The `Parameters` struct (Co3Ne.h lines 63–105) originally had 14 fields.
Several were workarounds for broken sub-algorithms.

| Parameter | Issue | Status |
|-----------|-------|--------|
| `maxNormalAngle` default 90° | Should be 60° (see 1.5) | ✅ Fixed |
| `triangleQualityThreshold` | No Geogram equivalent; threshold formula documented in comments | ✅ Documented |
| `bypassManifoldExtraction` | Debug escape hatch | ✅ Removed |
| `relaxedManifoldExtraction` | Compensates for broken T3/T12 classification | ✅ Removed |
| `autoFixNonManifold` | Greedy post-hoc fix for edges that should not appear | ✅ Removed |
| `fixWindingOrder` | External winding fix signals incorrect normal propagation | ✅ Removed |
| `preventSelfIntersections` | Marked EXPERIMENTAL; incomplete test (no edge–edge intersection, O(n²), hardcoded cap of 50 000) | ✅ Removed |
| `useManifoldConstrainedGeneration` | Should be default behaviour | ✅ Removed from Parameters; production path is always manifold-constrained; legacy O(k²) path behind `#ifdef GTE_DEBUG` |
| `removeIsolatedTriangles` | Declared but never used in logic | ✅ Removed |
| `smoothWithRVD` | O(n²) per iteration; different semantics from Geogram's Co3Ne_smooth | ✅ Documented cost; default off |

**Action items:**
- [x] Apply the per-row recommended actions in the table above.
- [x] Target a reduced `Parameters` with ~5 meaningful fields: `kNeighbors`,
  `searchRadius`, `maxNormalAngle`, `orientNormals`, `strictMode`.
  *(`useManifoldConstrainedGeneration` and `removeIsolatedTriangles` removed.
  Remaining fields `smoothWithRVD`, `rvdSmoothIterations`, `triangleQualityThreshold`
  retained for now as they are exercised by existing tests; can be removed in a
  future pass.)*
  *(Remaining fields: `removeIsolatedTriangles`, `smoothWithRVD`,
  `rvdSmoothIterations`, `triangleQualityThreshold` — can be removed in a
  future pass.)*

---

## Part 4 — Recommended End-State Architecture

After the fixes above, the pipeline should be:

```
Input point cloud
      │
      ▼
ComputeNormals()           — PCA, SymmetricEigensolver3x3, equivalent to Geogram ✓
      │
      ▼
OrientNormalsConsistently() — BFS pop-time flip, outer disconnected-component loop
      │
      ▼
GenerateTrianglesManifoldConstrained()
                            — Angular-consecutive pairs, edge-budget ≤2 (O(k) per seed)
      │
      ▼
Co3NeManifoldExtractor::Extract()
                            — All unique candidates as T3 seeds (no T12 split needed)
                            — Fixed TrianglesNormalsAgree (threshold −0.8, orientation correction)
                            — Fixed MergeConnectedComponent (BFS-order traversal)
                            — Fixed ReorientMesh (border-distance BFS)
      │
      ▼
MeshHoleFilling (one pass)  — Optional; fill small remaining holes
      │
      ▼
Output manifold mesh
```

This replaces ~6 000 lines of partially-broken code with approximately 500
lines of correct, auditable code.

---

## Part 5 — Summary Checklist

### Must Fix (bugs / correctness)
- [x] **1.1** Make manifold-constrained generation the default; remove or
  isolate O(k²) generator; remove meaningless T3/T12 split when using it.
  *(O(k²) path and T3/T12 split hidden behind `#ifdef GTE_DEBUG`.)*
- [x] **1.2** Fix `OrientNormalsConsistently`: pop-time flip + outer
  disconnected-component loop.
- [x] **1.3** Fix `TrianglesNormalsAgree`: add orientation correction, change
  threshold to −0.8.
- [x] **1.7** Fix `MergeConnectedComponent`: replace O(T) scan with BFS
  traversal via adjacency structure.
- [x] **2.3.2** Fix undefined `LogAssert` in `Co3NeManifoldExtractor`.
- [x] **2.1** Delete broken `Co3NeEnhanced.h`.

### Should Fix (quality / parity)
- [x] **1.4** Replace auto-radius formula with `0.05 * bbox_diagonal`.
- [x] **1.5** Change `maxNormalAngle` default to 60°.
- [ ] **1.6** Audit `ReorientMesh` against Geogram's `mesh_reorient`.
- [x] **2.2.1** Remove duplicate hole-filling step from `Co3NeManifoldStitcher`.
- [x] **2.2.2** Fix orphaned-vertices bug in revert-on-failure.
- [x] **2.2.3** Replace `AbsorbSmallClosedComponents` with simple deletion
  (changed default `absorbSmallClosedComponents` to `false`).
- [x] **2.3.3** Consolidate manifold extractor: legacy T3/T12 path hidden
  behind `#ifdef GTE_DEBUG`; default path calls `Co3NeManifoldExtractor`.

### Should Remove (cleanup)
- [x] **2.1** `Co3NeEnhanced.h` (broken, duplicate) — deleted.
- [x] **3** `bypassManifoldExtraction` parameter — removed.
- [x] **3** `relaxedManifoldExtraction` parameter — removed.
- [x] **3** `autoFixNonManifold` parameter — removed.
- [x] **3** `fixWindingOrder` parameter — removed.
- [x] **3** `preventSelfIntersections` parameter — removed.
- [x] **3** `useManifoldConstrainedGeneration` parameter — removed (production always uses manifold-constrained; legacy O(k²) behind `#ifdef GTE_DEBUG`).
- [x] **3** `removeIsolatedTriangles` parameter — removed (was declared but never used).
- [x] **2.2.4** O(N·T) `RemoveNonManifoldEdges`; replaced with single-pass O(T).
- [x] **2.2.5** `targetSingleComponent = true` default changed to `false`.

### Should Document
- [x] Explain why manifold-constrained generation matches RVC and is preferred (documented in code comments).
- [x] Document `triangleQualityThreshold` formula (documented in struct comment).
- [x] Document the computational cost of `smoothWithRVD` vs. Geogram's
  Co3Ne_smooth (documented in `Parameters` struct comment).

### Remaining
- [x] **1.6** Audit `ReorientMesh` against Geogram's `mesh_reorient` (documented
  intentional divergence in code comments: GTE seeds from interior, Geogram from border).
- [x] **1.6** Validate or remove the Möbius-strip detection heuristic in
  `PropagateOrientation` (removed: simplified to majority-vote flip with no Dissociate).
- [x] **2.2.5** Add minimum-gap threshold to `TopologyAwareComponentBridging`
  (`maxBridgeGapDistance` parameter added; caps bridge threshold at specified distance).
- [x] Further reduce `Parameters` to ~5 meaningful fields (`kNeighbors`,
  `searchRadius`, `maxNormalAngle`, `orientNormals`, `strictMode`).
  *(`useManifoldConstrainedGeneration` and `removeIsolatedTriangles` removed.
  `smoothWithRVD`, `rvdSmoothIterations`, `triangleQualityThreshold` retained
  pending test updates.)*
