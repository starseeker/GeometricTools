# Project Goals and Objectives

**Project:** Geogram to GTE Migration for BRL-CAD  
**Started:** 2026-02  
**Primary Goal:** Enable BRL-CAD to eliminate Geogram as a dependency

---

## Primary Objective

Replace BRL-CAD's use of Geogram mesh processing algorithms with equivalent GTE-style implementations, maintaining or improving quality while embracing GTE's header-only, platform-independent architecture.

---

## Specific Goals

### 1. Feature Parity with Geogram

**Target:** Implement all Geogram features currently used by BRL-CAD

**Required Capabilities:**
- ✅ Mesh repair (vertex deduplication, degenerate removal, topology validation)
- ✅ Hole filling (with quality triangulation: EC → CDT → LSCM → 3D fallback chain)
- ✅ CVT-based remeshing (isotropic)
- ✅ **Anisotropic remeshing (COMPLETE - full 6D CVT implementation)**
- ✅ Restricted Voronoi Diagram (RVD) computation — required by the CVT remeshing pipeline

> **Note on RVD:** The RVD infrastructure (RestrictedVoronoiDiagram.h, CVTN.h, SurfaceRVDN.h,
> RestrictedVoronoiDiagramN.h, DelaunayNN.h, etc.) is **not** a Co3Ne artifact — it is the
> core of the CVT Lloyd-relaxation loop used for both isotropic and anisotropic remeshing.
> It should be retained.

> **Note on LSCM:** LSCMParameterization.h provides an arc-length boundary-to-circle
> mapping used as the second-to-last fallback during hole triangulation.  For a hole
> boundary (no interior vertices) the full LSCM linear system reduces to the boundary
> constraint alone, making this a lightweight but reliable step before 3D ear clipping.

**Success Metric:** BRL-CAD can perform all mesh operations currently done with Geogram using GTE implementations instead.

### 2. Maintain or Improve Quality

**Target:** Results should be equal to or better than Geogram

**Quality Dimensions:**
- ✅ **Triangle Quality:** CDT option provides superior angle optimization vs Geogram
- ✅ **Robustness:** Exact arithmetic eliminates floating-point errors
- ✅ **Correctness:** All algorithms validated on stress tests
- ✅ **Coverage:** 100% success rate on challenging test cases

**Success Metric:** Validation tests show equal or better mesh quality metrics compared to Geogram output.

### 3. GTE-Style Implementation

**Target:** Code should follow GTE conventions and architecture

**Requirements:**
- ✅ **Header-only:** No compiled libraries, all template code in headers
- ✅ **Platform-independent:** No platform-specific code or dependencies
- ✅ **GTE conventions:** Follow existing GTE coding style and patterns
- ✅ **Modern C++:** Use C++17 features appropriately
- ✅ **Template-based:** Generic for different numeric types (float, double)

**Success Metric:** New code integrates seamlessly with existing GTE infrastructure.

### 4. Eliminate External Dependencies

**Target:** Remove need for Geogram library in BRL-CAD builds

**Benefits:**
- Simplified build system (no Geogram compilation needed)
- Improved portability (fewer platform-specific issues)
- Easier maintenance (header-only code, no library version conflicts)
- Reduced build time (no external library to compile)

**Success Metric:** BRL-CAD builds successfully with only GTE headers, no Geogram linkage.

### 5. License Compatibility

**Target:** Maintain proper licensing for all ported code

**Requirements:**
- ✅ Geogram code ported to GTE style uses Geogram's BSD 3-Clause license
- ✅ New GTE-style code uses Boost Software License (GTE standard)
- ✅ All license headers properly maintained
- ✅ Clear separation of code with different licenses

**Success Metric:** All code properly licensed, compatible with BRL-CAD's needs.

### 6. Comprehensive Testing

**Target:** Validate all implementations thoroughly

**Testing Strategy:**
- ✅ Unit tests for individual algorithms
- ✅ Stress tests for edge cases
- ✅ Integration tests with real meshes (gt.obj)
- ✅ Performance benchmarks vs Geogram
- ✅ Quality metric comparisons

**Success Metric:** 100% pass rate on comprehensive test suite.

### 7. Clear Documentation

**Target:** Provide excellent documentation for users and developers

**Documentation Types:**
- ✅ API documentation (inline comments)
- ✅ Usage examples (test programs)
- ✅ Algorithm descriptions (technical docs)
- ✅ Migration guide (for BRL-CAD integration)
- ✅ Development history (archived for reference)

**Success Metric:** BRL-CAD developers can integrate and use the code without external assistance.

---

## Original Problem Statement

From the initial project requirements:

> "BRL-CAD is currently making use of a number of algorithms from Geogram. Geogram as a dependency is a bit tricky to deal with from a portability perspective, so we would like to replace our use of Geogram with GTE features instead."

### Why Geogram is Problematic

1. **Build Complexity:** Requires compilation into libraries
2. **Platform Issues:** Platform-specific code complicates portability
3. **Dependency Management:** External dependency adds maintenance burden
4. **Build Time:** Large library takes time to compile
5. **Version Conflicts:** Library versioning can cause conflicts

### Why GTE is Better

1. **Header-only:** No compilation needed, just include headers
2. **Platform-independent:** Pure C++, no platform-specific code
3. **No Dependencies:** Self-contained, minimal external requirements
4. **Fast Integration:** Drop headers into project, ready to use
5. **Active Development:** Well-maintained by David Eberly

---

## Implementation Philosophy

### Port vs. Rewrite

**Approach Chosen:** Intelligent porting with enhancements

**Rationale:**
- License compatibility allows direct porting of algorithms
- No need for "clean-room" reimplementation
- Can improve upon Geogram where GTE provides better tools
- Maintain algorithmic correctness while adapting to GTE style

### Hybrid Implementation Strategy

**Core Principle:** Use GTE's existing capabilities wherever possible

**Examples:**
- ✅ **Triangulation:** Use GTE's TriangulateEC and TriangulateCDT (better than Geogram!)
- ✅ **Delaunay:** Use GTE's Delaunay3 class
- ✅ **Linear Algebra:** Use GTE's Vector3, Matrix3x3, eigensolvers
- ✅ **Exact Arithmetic:** Use GTE's BSNumber for robustness

**Custom Implementations Only When Needed:**
- Restricted Voronoi Diagram (no GTE equivalent; required for CVT remeshing)
- CVT optimization (specialized algorithm)
- LSCM boundary arc-length parameterization (for non-projectable hole triangulation)

