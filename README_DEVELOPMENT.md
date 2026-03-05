# Developer Guide - GTE Mesh Processing

**Target Audience:** Developers working with or extending the GTE mesh processing implementation  
**Last Updated:** 2026-02-11

---

## Getting Started

### Prerequisites

**Compiler Requirements:**
- C++17 compliant compiler
- GCC 7.5+, Clang 5.0+, or MSVC 2017+
- Standard library with threading support (`<thread>`, `<mutex>`, `<condition_variable>`)

**Dependencies:**
- GTE (Geometric Tools Engine) headers
- No external libraries required beyond standard C++17

### Repository Structure

```
GeometricTools/
├── GTE/                           # GTE library headers
│   └── Mathematics/              # Math components
│       ├── MeshRepair.h         # Mesh repair operations
│       ├── MeshHoleFilling.h    # Hole filling algorithms
│       ├── MeshPreprocessing.h  # Mesh preprocessing
│       ├── MeshRemesh.h         # CVT remeshing
│       ├── MeshAnisotropy.h     # Anisotropic remeshing support
│       ├── SurfaceRVDN.h        # N-D surface RVD
│       ├── CVTN.h               # N-D CVT
│       ├── RestrictedVoronoiDiagram.h  # RVD base
│       ├── RestrictedVoronoiDiagramOptimized.h  # Optimized RVD
│       ├── CVTOptimizer.h       # CVT optimization
│       ├── IntegrationSimplex.h # Integration utilities
│       └── ThreadPool.h         # Thread pool
├── tests/                        # Test programs and data
│   ├── README.md                # Test documentation
│   ├── *.cpp                    # Test programs
│   ├── data/                    # Test data files
│   │   └── *.obj               # Test meshes
│   └── scripts/                 # Test scripts
│       ├── *.py                # Mesh generation scripts
│       └── *.sh                # Test runner scripts
├── docs/                         # Documentation
│   └── archive/                 # Historical documentation
├── STATUS.md                     # Current implementation status
├── GOALS.md                      # Project goals
├── UNIMPLEMENTED.md             # Remaining features
├── README_DEVELOPMENT.md        # This file
├── README.md                     # Main readme (GTE)
└── Makefile                      # Build configuration
```

### Quick Start

1. **Clone the repository:**
```bash
git clone https://github.com/starseeker/GeometricTools.git
cd GeometricTools
```

2. **Initialize submodules** (geogram and PoissonRecon):
```bash
git submodule update --init
# geogram also requires its own sub-submodules for building:
cd geogram && git submodule update --init \
    src/lib/geogram/third_party/OpenNL \
    src/lib/geogram/third_party/rply \
    src/lib/geogram/third_party/libMeshb \
    src/lib/geogram/third_party/amgcl
cd ..
```

3. **Build all tests:**
```bash
make all
```

4. **Run basic validation (tests are in tests subdirectory):**
```bash
make test
# Or manually:
./test_mesh_repair gt.obj gt_repaired.obj
```

5. **Run stress tests:**
```bash
./stress_test
```

6. **Run GTE vs Geogram comparison** (verify GTE implementation correctness):
```bash
# First build geogram (one-time setup):
mkdir -p /tmp/geogram_build && cd /tmp/geogram_build
cmake <repo>/geogram -DGEOGRAM_WITH_GRAPHICS=OFF -DGEOGRAM_WITH_LEGACY_NUMERICS=OFF -DGEOGRAM_WITH_LUA=OFF
make -j$(nproc) geogram
cd <repo>

# Then build and run the comparison test:
make test_geogram_comparison
make test_geogram    # runs ./test_geogram_comparison tests/data/gt.obj
```

---

## Core Components

### 1. MeshRepair.h

**Purpose:** Basic mesh repair operations

**Key Classes:**
- `MeshRepair<Real>` - Main repair class

**Key Operations:**
- `RemoveDuplicateVertices()` - Merge vertices at same location
- `RemoveDegenerateTriangles()` - Remove zero-area triangles
- `RemoveIsolatedVertices()` - Clean up unused vertices
- `DetectSelfIntersections()` - Find intersecting triangles
- `ValidateTopology()` - Check mesh consistency

**Usage Example:**
```cpp
#include <GTE/Mathematics/MeshRepair.h>

std::vector<Vector3<double>> vertices;
std::vector<std::array<int32_t, 3>> triangles;

// Load mesh...

MeshRepair<double>::Parameters params;
params.epsilon = 1e-6;  // Vertex merge tolerance
params.repair = true;   // Apply repairs

MeshRepair<double>::Repair(vertices, triangles, params);
```

### 2. MeshHoleFilling.h

**Purpose:** Triangulate holes in meshes

**Key Classes:**
- `MeshHoleFilling<Real>` - Main hole filling class

**Triangulation Methods:**
1. **Ear Clipping (2D)** - `TriangulationMethod::EarClipping`
   - Projects to 2D, uses GTE's exact arithmetic
   - Fast, robust for planar holes

2. **Constrained Delaunay (2D)** - `TriangulationMethod::CDT` ⭐ RECOMMENDED
   - Projects to 2D, optimizes triangle angles
   - Best quality, slightly slower

3. **Ear Clipping (3D)** - `TriangulationMethod::EarClipping3D`
   - Direct 3D computation, no projection
   - Better for highly non-planar holes

**Usage Example:**
```cpp
#include <GTE/Mathematics/MeshHoleFilling.h>

MeshHoleFilling<double>::Parameters params;
params.method = TriangulationMethod::CDT;  // Use CDT
params.autoFallback = true;                 // Auto-switch for non-planar
params.planarityThreshold = 0.2;           // Non-planarity threshold
params.repair = true;                       // Clean up after
params.maxArea = 1e30;                     // Fill all holes
params.maxEdges = std::numeric_limits<size_t>::max();

MeshHoleFilling<double>::FillHoles(vertices, triangles, params);
```

### 3. MeshRemesh.h & CVTOptimizer.h

**Purpose:** CVT-based mesh remeshing

**Key Classes:**
- `MeshRemesh<Real>` - Main remeshing class
- `CVTOptimizer<Real>` - CVT optimization engine

**Algorithm Steps:**
1. **Sample Points** - Generate initial point distribution
2. **Lloyd Relaxation** - Iteratively move points to centroids
3. **Newton Optimization** - (Optional) Faster convergence
4. **Surface Projection** - Project to original surface
5. **Mesh Extraction** - Generate final triangulation

**Usage Example:**
```cpp
#include <GTE/Mathematics/MeshRemesh.h>

std::vector<Vector3<double>> vertices;
std::vector<std::array<int32_t, 3>> triangles;
// Load mesh...

MeshRemesh<double>::Parameters params;
params.targetVertexCount = 1000;  // Target vertex count
params.lloydIterations   = 10;    // Lloyd iterations
params.projectToSurface  = true;  // Project to original

MeshRemesh<double>::RemeshCVT(vertices, triangles, params);
```

### 4. RestrictedVoronoiDiagram.h

**Purpose:** Compute Voronoi diagram restricted to surface

**Key Classes:**
- `RestrictedVoronoiDiagram<Real>` - Base RVD
- `RestrictedVoronoiDiagramOptimized<Real>` - Optimized version with AABB tree

**Usage Example:**
```cpp
#include <GTE/Mathematics/RestrictedVoronoiDiagramOptimized.h>

std::vector<Vector3<double>> vertices;
std::vector<std::array<int32_t, 3>> triangles;
std::vector<Vector3<double>> sites;  // Voronoi sites

RestrictedVoronoiDiagramOptimized<double> rvd(vertices, triangles);

for (size_t i = 0; i < sites.size(); ++i) {
    auto cell = rvd.ComputeCell(i, sites);
    double mass = cell.mass;
    Vector3<double> centroid = cell.centroid;
    // Use cell data...
}
```

### 6. ThreadPool.h

**Purpose:** Parallel processing support

**Key Classes:**
- `ThreadPool` - Work queue thread pool

**Usage Example:**
```cpp
#include <GTE/Mathematics/ThreadPool.h>

ThreadPool pool(std::thread::hardware_concurrency());

std::vector<std::future<Result>> futures;
for (auto& task : tasks) {
    futures.push_back(pool.enqueue([&task]() {
        return ProcessTask(task);
    }));
}

for (auto& future : futures) {
    auto result = future.get();
    // Use result...
}
```

---

## Code Style and Conventions

### Follow GTE Patterns

**1. Header-Only Implementation**
- All code in header files (no .cpp files)
- Templates for generic numeric types

**2. Naming Conventions**
```cpp
// Classes: PascalCase
class MeshRepair { };

// Methods: PascalCase
void RemoveDuplicates();

// Variables: camelCase
int numVertices;

// Constants: ALL_CAPS
constexpr int MAX_ITERATIONS = 100;
```

**3. Template Parameters**
```cpp
template <typename Real>
class MyClass {
    static_assert(std::is_floating_point<Real>::value,
                  "Real must be a floating-point type");
};
```

**4. Namespace**
```cpp
namespace gte {
    // All code here
}
```

**5. Documentation**
```cpp
// Single-line comment for brief notes

/*
 * Multi-line comment for detailed explanations
 * Include algorithm descriptions
 * Cite papers if applicable
 */

/**
 * @brief Brief description
 * @param vertices Input mesh vertices
 * @return Repaired mesh
 */
```

---

## Performance Considerations

### Optimization Guidelines

**1. Use Appropriate Data Structures**
- `std::vector` for dynamic arrays
- `std::unordered_map` for hash tables
- `std::set` for sorted unique elements
- Reserve capacity when size known

**2. Avoid Unnecessary Copies**
```cpp
// Bad
void Process(std::vector<Vector3<double>> vertices);

// Good
void Process(std::vector<Vector3<double>> const& vertices);
```

**3. Use Move Semantics**
```cpp
return std::move(result);  // When appropriate
```

**4. Parallel Processing**
```cpp
// Use ThreadPool for independent tasks
ThreadPool pool;
for (auto& task : tasks) {
    pool.enqueue([&task]() { ProcessTask(task); });
}
```

**5. Profile Before Optimizing**
```bash
# Use profiling tools
g++ -pg -O2 ... && gprof ...
perf record ./program && perf report
```

---

## Common Development Tasks

### Adding a New Algorithm

1. **Create header file:**
```cpp
// GTE/Mathematics/MyAlgorithm.h
#pragma once

#include <GTE/Mathematics/Vector3.h>
#include <vector>

namespace gte {

template <typename Real>
class MyAlgorithm {
public:
    struct Parameters {
        Real tolerance = static_cast<Real>(1e-6);
        // ...
    };
    
    static void Process(
        std::vector<Vector3<Real>> const& input,
        std::vector<Vector3<Real>>& output,
        Parameters const& params);
};

} // namespace gte

#include "MyAlgorithm.inl"  // Implementation
```

2. **Create test program:**
```cpp
// tests/test_my_algorithm.cpp
#include <GTE/Mathematics/MyAlgorithm.h>
#include <iostream>

int main() {
    // Test code
    return 0;
}
```

3. **Add to Makefile:**
```makefile
test_my_algorithm: tests/test_my_algorithm.cpp
	$(CXX) $(CXXFLAGS) -o test_my_algorithm tests/test_my_algorithm.cpp
```

4. **Document in tests/README.md**

### Debugging Tips

**1. Enable Debug Output**
```cpp
#define DEBUG_OUTPUT
#ifdef DEBUG_OUTPUT
    std::cout << "Debug: " << info << std::endl;
#endif
```

**2. Validate Intermediate Results**
```cpp
assert(vertices.size() > 0);
assert(IsValidMesh(vertices, triangles));
```

**3. Use GDB/LLDB**
```bash
g++ -g -O0 test.cpp -o test
gdb ./test
> break main
> run
> print variable
```

**4. Visualization**
- Output intermediate meshes as OBJ
- Use MeshLab or Blender to visualize
- Check for visual artifacts

---

## Integration with BRL-CAD

### Replacing Geogram Calls

**Before (Geogram):**
```cpp
#include <geogram/mesh/mesh_repair.h>

GEO::Mesh mesh;
// ... load mesh ...
GEO::mesh_repair(mesh, GEO::MESH_REPAIR_DEFAULT);
GEO::fill_holes(mesh, max_area, max_edges);
```

**After (GTE):**
```cpp
#include <GTE/Mathematics/MeshRepair.h>
#include <GTE/Mathematics/MeshHoleFilling.h>

std::vector<Vector3<double>> vertices;
std::vector<std::array<int32_t, 3>> triangles;
// ... load mesh ...

MeshRepair<double>::Parameters repairParams;
MeshRepair<double>::Repair(vertices, triangles, repairParams);

MeshHoleFilling<double>::Parameters fillParams;
fillParams.maxArea = max_area;
fillParams.maxEdges = max_edges;
MeshHoleFilling<double>::FillHoles(vertices, triangles, fillParams);
```

---

## Contributing

### Before Submitting

1. **Ensure code compiles:**
```bash
make clean && make all
```

2. **Run tests:**
```bash
./stress_test
make test
```

3. **Check for warnings:**
```bash
g++ -std=c++17 -Wall -Wextra -Wpedantic ...
```

4. **Update documentation:**
- Add inline comments
- Update STATUS.md if adding features
- Update tests/README.md for new tests

5. **Follow GTE style:**
- Use GTE naming conventions
- Header-only implementation
- Template-based design

---

## Troubleshooting

### Common Issues

**1. Compilation Errors:**
```
error: 'gte' has not been declared
```
**Solution:** Add `-I.` to include current directory

**2. Linking Errors:**
```
undefined reference to pthread_create
```
**Solution:** Add `-pthread` flag

**3. Runtime Crashes:**
```
Segmentation fault
```
**Solution:** Check array bounds, validate input data

**4. Poor Performance:**
**Solution:** Use optimized build (`-O2` or `-O3`), enable parallelization

---

## Resources

### Documentation
- **GOALS.md** - Project objectives
- **UNIMPLEMENTED.md** - Remaining features
- **tests/README.md** - Test documentation
- **docs/archive/** - Historical documentation

### External References
- [GTE Documentation](https://www.geometrictools.com)
- [Geogram Source](https://github.com/BrunoLevy/geogram)
- C++17 Reference

### Getting Help
- Review archived documentation in docs/archive/
- Examine test programs for usage examples
- Check inline code comments
- Refer to GTE documentation for base functionality

---

## Conclusion

This guide provides the essential information for developing and extending the GTE mesh processing implementation. For specific algorithm details, see the individual header files and archived documentation.

**Happy Coding!** 🚀
