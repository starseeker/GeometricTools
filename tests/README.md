# Test Suite Documentation

**Last Updated:** 2026-02-11  
**Purpose:** Documentation for all test programs and test data

---

## Overview

This directory contains comprehensive tests for the GTE mesh processing implementation. Tests are organized into categories: basic functionality, stress testing, performance benchmarking, and comparison with Geogram.

---

## Directory Structure

```
tests/
├── README.md                    # This file
├── *.cpp                        # Test programs
├── data/                        # Test data files
│   ├── *.obj                   # Test meshes
│   └── *.txt                   # Test results
└── scripts/                     # Test generation scripts
    ├── *.py                    # Python mesh generators
    └── *.sh                    # Shell test runners
```

---

## Core Functionality Tests

### test_mesh_repair.cpp
**Purpose:** Basic mesh repair operations  
**What it tests:**
- Vertex deduplication
- Degenerate triangle removal
- Isolated vertex cleanup
- Basic hole filling

**Usage:**
```bash
./test_mesh_repair <input.obj> <output.obj>
```

**Example:**
```bash
./test_mesh_repair data/gt.obj data/gt_repaired.obj
```

**Exercises:** MeshRepair.h, MeshHoleFilling.h, MeshPreprocessing.h

---

### test_remesh.cpp
**Purpose:** CVT-based remeshing  
**What it tests:**
- Lloyd relaxation
- CVT optimization
- Mesh simplification/densification
- Surface projection

**Usage:**
```bash
./test_remesh <input.obj> <output.obj> <num_samples>
```

**Example:**
```bash
./test_remesh data/gt.obj data/gt_remeshed.obj 1000
```

**Exercises:** MeshRemeshFull.h, CVTOptimizer.h, RestrictedVoronoiDiagram.h

---

### test_co3ne.cpp
**Purpose:** Surface reconstruction from point clouds  
**What it tests:**
- Normal estimation (PCA-based)
- Normal orientation propagation
- Co-cone triangle generation
- Manifold extraction

**Usage:**
```bash
./test_co3ne <points.obj> <output.obj>
```

**Example:**
```bash
./test_co3ne data/points.obj data/reconstructed.obj
```

**Exercises:** Co3NeFull.h

---

### test_rvd.cpp
**Purpose:** Restricted Voronoi Diagram computation  
**What it tests:**
- RVD cell computation
- Surface restriction
- Integration (mass, centroids)
- Polygon clipping

**Usage:**
```bash
./test_rvd <mesh.obj> <num_sites>
```

**Example:**
```bash
./test_rvd data/gt.obj 100
```

**Exercises:** RestrictedVoronoiDiagram.h, IntegrationSimplex.h

---

### test_full_algorithms.cpp
**Purpose:** Comprehensive test of all algorithms  
**What it tests:**
- Complete Co3Ne pipeline
- Complete remeshing pipeline
- Integration of all components
- End-to-end validation

**Usage:**
```bash
./test_full_algorithms
```

**Exercises:** Co3NeFull.h, MeshRemeshFull.h, all core components

---

## Stress and Edge Case Tests

### stress_test.cpp ⭐ COMPREHENSIVE
**Purpose:** Test all triangulation methods on challenging cases  
**What it tests:**
- Highly non-planar holes (spherical wrapping)
- Nearly degenerate vertices (collinear)
- Elongated holes (100:1 aspect ratio)
- Large complex holes (100+ vertices)
- Non-planar holes on curved surfaces

**Methods tested:**
1. Ear Clipping (2D with exact arithmetic)
2. Constrained Delaunay Triangulation (CDT)
3. Ear Clipping 3D (direct 3D)

**Usage:**
```bash
./stress_test
```

**Output:** Detailed results for each test and method, success/failure status

**Exercises:** All three triangulation methods in MeshHoleFilling.h

---

### test_enhanced_manifold.cpp
**Purpose:** Test enhanced manifold extraction  
**What it tests:**
- Manifold topology validation
- Non-manifold edge detection
- Boundary consistency
- Complex topology handling

**Usage:**
```bash
./test_enhanced_manifold
```

**Exercises:** Manifold extraction components

---

### test_euler_investigation.cpp
**Purpose:** Investigate Euler characteristic issues  
**What it tests:**
- Euler characteristic calculation (V - E + F = 2)
- Topology validation
- Edge case handling

**Usage:**
```bash
./test_euler_investigation
```

**Exercises:** Topology validation in mesh repair

---

### test_closure_diagnostic.cpp
**Purpose:** Diagnose hole closure issues  
**What it tests:**
- Hole boundary detection
- Closure validation
- Edge connectivity

**Usage:**
```bash
./test_closure_diagnostic
```

**Exercises:** Hole detection and filling logic

---

### test_timeout_closure.cpp
**Purpose:** Test timeout-based closure mechanisms  
**What it tests:**
- Timeout handling for complex holes
- Fallback mechanisms
- Robustness under time constraints

**Usage:**
```bash
./test_timeout_closure
```

**Exercises:** Timeout and fallback mechanisms

---

## Performance and Optimization Tests

### test_rvd_performance.cpp ⭐ BENCHMARK
**Purpose:** Benchmark RVD computation performance  
**What it tests:**
- RVD computation speed
- Scaling with mesh size
- Scaling with number of sites
- Comparison with Geogram (if available)

**Usage:**
```bash
./test_rvd_performance <mesh.obj> <num_sites>
```

**Example:**
```bash
./test_rvd_performance data/gt.obj 1000
```

**Output:** Timing data, performance metrics

**Exercises:** RestrictedVoronoiDiagramOptimized.h

---

### test_parallel_rvd.cpp
**Purpose:** Test parallel RVD computation  
**What it tests:**
- ThreadPool functionality
- Parallel RVD cell computation
- Speedup vs serial
- Thread safety

**Usage:**
```bash
./test_parallel_rvd <mesh.obj> <num_sites>
```

**Exercises:** ThreadPool.h, parallel RVD computation

---

### test_threadpool.cpp
**Purpose:** Test ThreadPool implementation  
**What it tests:**
- Work queue functionality
- Task distribution
- Thread synchronization
- Performance vs serial

**Usage:**
```bash
./test_threadpool
```

**Exercises:** ThreadPool.h

---

### test_newton_optimizer.cpp
**Purpose:** Test Newton/BFGS optimization  
**What it tests:**
- Newton optimization convergence
- BFGS updates
- Line search
- Comparison with Lloyd-only

**Usage:**
```bash
./test_newton_optimizer
```

**Exercises:** CVTOptimizer.h

---

## Comparison and Validation Tests

### compare_with_geogram.cpp
**Purpose:** Direct comparison with Geogram results  
**What it tests:**
- Output quality comparison
- Performance comparison
- Feature parity validation

**Usage:**
```bash
./compare_with_geogram <mesh.obj>
```

**Note:** Requires Geogram to be available

**Exercises:** Cross-validation with Geogram

---

### test_remesh_comparison.cpp
**Purpose:** Compare remeshing methods  
**What it tests:**
- Lloyd relaxation vs Newton optimization
- Approximate Voronoi vs exact RVD
- Quality metrics comparison

**Usage:**
```bash
./test_remesh_comparison
```

**Exercises:** Different remeshing strategies

---

### test_with_validation.cpp
**Purpose:** Run tests with extensive validation  
**What it tests:**
- Mesh validity after operations
- Topology consistency
- Geometric correctness

**Usage:**
```bash
./test_with_validation
```

**Exercises:** Validation and correctness checking

---

## Algorithm Variant Tests

### test_co3ne_simple.cpp
**Purpose:** Simplified Co3Ne test  
**What it tests:**
- Basic Co3Ne without advanced features
- Minimal test case
- Quick validation

**Usage:**
```bash
./test_co3ne_simple
```

**Exercises:** Basic Co3Ne functionality

---

### test_co3ne_debug.cpp
**Purpose:** Debug version of Co3Ne test  
**What it tests:**
- Step-by-step algorithm execution
- Intermediate results
- Debugging aids

**Usage:**
```bash
./test_co3ne_debug
```

**Exercises:** Co3Ne with detailed output

---

### test_co3ne_rvd.cpp
**Purpose:** Co3Ne with RVD integration  
**What it tests:**
- Co3Ne using RVD for Voronoi computation
- Integration between components
- Quality comparison

**Usage:**
```bash
./test_co3ne_rvd
```

**Exercises:** Co3Ne + RVD integration

---

### test_no_repair.cpp
**Purpose:** Test without mesh repair  
**What it tests:**
- Behavior on non-repaired meshes
- Error handling
- Robustness

**Usage:**
```bash
./test_no_repair
```

**Exercises:** Operations without preprocessing

---

### test_manifold_closure.cpp, test_manifold_test.cpp
**Purpose:** Manifold-specific tests  
**What it tests:**
- Manifold extraction
- Closure operations
- Topology preservation

**Usage:**
```bash
./test_manifold_closure
./test_manifold_test
```

**Exercises:** Manifold handling

---

## Debugging and Development Tests

### test_debug.cpp
**Purpose:** General debugging test  
**What it tests:**
- Quick ad-hoc testing
- Debugging specific issues
- Development experiments

**Usage:**
```bash
./test_debug
```

---

### test_debug_50points.cpp
**Purpose:** Small point cloud test  
**What it tests:**
- Co3Ne on minimal input
- Fast iteration during development
- Edge case: very small point clouds

**Usage:**
```bash
./test_debug_50points
```

---

### check_projection.cpp
**Purpose:** Validate projection operations  
**What it tests:**
- 3D to 2D projection
- Projection quality
- Planarity validation

**Usage:**
```bash
./check_projection
```

**Exercises:** Projection logic in hole filling

---

## Demonstration Programs

### demo_rvd_cvt.cpp
**Purpose:** Demonstration of RVD and CVT  
**What it tests:**
- Interactive demonstration
- Visualization-ready output
- Example usage

**Usage:**
```bash
./demo_rvd_cvt
```

**Exercises:** Complete RVD/CVT pipeline

---

## Test Data Files

### data/gt.obj ⭐ PRIMARY TEST MESH
**Description:** Main test mesh with various challenges  
**Properties:**
- Multiple non-manifold features
- Holes of various sizes
- Self-intersections
- Degenerate triangles
**Used by:** Most tests

---

### data/stress_*.obj
**Description:** Stress test meshes  
**Variants:**
- `stress_concave.obj` - Concave star-shaped holes
- `stress_degenerate.obj` - Nearly collinear vertices
- `stress_elongated.obj` - 100:1 aspect ratio holes
- `stress_large.obj` - Large complex holes (100+ vertices)
- `stress_nonplanar.obj` - Non-planar holes
- `stress_wrapped_sphere.obj` - Wrapping holes
- And more...

**Created by:** scripts/create_stress_meshes.py

---

### data/test_*.obj
**Description:** Various test cases  
**Examples:**
- `test_tiny.obj` - Minimal test case
- `test_bowtie.obj` - Bowtie-shaped degeneracy
- `test_sphere_enhanced.obj` - Sphere with enhancements
- `test_ec3d.obj` - Ear clipping 3D test

---

### data/*_ec3d.obj, *_cdt.obj
**Description:** Output from specific methods  
**Naming convention:**
- `*_ec3d.obj` - Ear clipping 3D output
- `*_cdt.obj` - CDT output
- `*_output.obj` - General output

---

### data/*.txt
**Description:** Test results and benchmarks  
**Files:**
- `stress_results.txt` - Stress test results
- `stress_test_results.txt` - Detailed stress test data

---

## Test Generation Scripts

### scripts/create_stress_meshes.py ⭐ PRIMARY GENERATOR
**Purpose:** Generate comprehensive stress test cases  
**Creates:**
- Concave holes
- Degenerate configurations
- Elongated holes
- Large complex holes
- Non-planar holes

**Usage:**
```bash
python3 scripts/create_stress_meshes.py
```

**Output:** Multiple .obj files in data/

---

### scripts/create_self_intersecting_test.py
**Purpose:** Generate self-intersecting meshes  
**Creates:** Meshes with controlled self-intersections for testing repair

**Usage:**
```bash
python3 scripts/create_self_intersecting_test.py
```

---

### scripts/create_wrapped_sphere_hole.py
**Purpose:** Generate holes wrapping around sphere  
**Creates:** Non-planar holes on curved surfaces

**Usage:**
```bash
python3 scripts/create_wrapped_sphere_hole.py
```

---

### scripts/create_true_self_intersecting.py
**Purpose:** Generate true self-intersecting topology  
**Creates:** Complex self-intersection cases

**Usage:**
```bash
python3 scripts/create_true_self_intersecting.py
```

---

### scripts/create_true_wrapped_hole.py
**Purpose:** Generate complex wrapped holes  
**Creates:** Challenging hole topologies

**Usage:**
```bash
python3 scripts/create_true_wrapped_hole.py
```

---

### scripts/create_extreme_self_intersect.py
**Purpose:** Generate extreme self-intersection cases  
**Creates:** Worst-case self-intersections for stress testing

**Usage:**
```bash
python3 scripts/create_extreme_self_intersect.py
```

---

### scripts/run_stress_tests.sh
**Purpose:** Automated stress test runner  
**Runs:** All stress tests in sequence

**Usage:**
```bash
bash scripts/run_stress_tests.sh
```

**Output:** Aggregated test results

---

## Running All Tests

### Quick Validation
```bash
# Build all tests
make all

# Run basic test
make test

# Run stress tests
./stress_test
```

### Comprehensive Testing
```bash
# Run all stress tests
bash scripts/run_stress_tests.sh

# Run performance benchmarks
./test_rvd_performance data/gt.obj 1000

# Run comparison tests
./compare_with_geogram data/gt.obj
```

### Generate Fresh Test Data
```bash
# Generate all stress test meshes
python3 scripts/create_stress_meshes.py

# Generate specific test cases
python3 scripts/create_self_intersecting_test.py
python3 scripts/create_wrapped_sphere_hole.py
```

---

## Test Results Interpretation

### Success Criteria
- ✅ All stress tests pass (100% success rate)
- ✅ Output meshes are valid (no degenerate triangles)
- ✅ Topology is consistent (Euler characteristic correct)
- ✅ Performance within acceptable range

### What Tests Validate
1. **Correctness:** Algorithms produce valid output
2. **Robustness:** Handle edge cases without crashing
3. **Quality:** Output meets quality standards
4. **Performance:** Execution time is reasonable
5. **Compatibility:** Results match or exceed Geogram

---

## Adding New Tests

### Template for New Test
```cpp
#include <GTE/Mathematics/YourAlgorithm.h>
#include <iostream>
#include <vector>

int main() {
    std::cout << "Testing: Your Algorithm" << std::endl;
    
    // Setup test data
    // ...
    
    // Run algorithm
    // ...
    
    // Validate results
    // ...
    
    std::cout << "Test: PASSED" << std::endl;
    return 0;
}
```

### Checklist
1. Create .cpp file in tests/
2. Add to Makefile
3. Document in this README.md
4. Create test data if needed (scripts/)
5. Run and validate

---

## Conclusion

This comprehensive test suite validates all aspects of the GTE mesh processing implementation. The tests ensure correctness, robustness, and performance, providing confidence in the implementation's production readiness.

**Test Coverage:** ✅ Comprehensive  
**Validation:** ✅ Extensive  
**Quality Assurance:** ✅ High
