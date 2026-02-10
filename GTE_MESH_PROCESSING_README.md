# GTE Mesh Processing Headers

This directory contains GTE-style implementations of mesh processing algorithms ported from Geogram, designed as header-only additions to the Geometric Tools Engine framework.

## Quick Start

```cpp
#include <GTE/Mathematics/MeshRepair.h>
#include <GTE/Mathematics/MeshHoleFilling.h>
#include <GTE/Mathematics/MeshPreprocessing.h>

// Your mesh data
std::vector<gte::Vector3<double>> vertices;
std::vector<std::array<int32_t, 3>> triangles;

// Step 1: Repair topology
gte::MeshRepair<double>::Parameters repairParams;
repairParams.epsilon = 1e-6 * boundingBoxDiagonal * 0.01;
gte::MeshRepair<double>::Repair(vertices, triangles, repairParams);

// Step 2: Fill holes
gte::MeshHoleFilling<double>::Parameters fillParams;
fillParams.maxArea = 1e30; // Fill all holes
gte::MeshHoleFilling<double>::FillHoles(vertices, triangles, fillParams);

// Step 3: Clean up small components
gte::MeshPreprocessing<double>::RemoveSmallComponents(vertices, triangles, minArea, 0);
```

## Available Headers

### MeshRepair.h
Repairs mesh topology issues:
- Merges duplicate vertices (with epsilon tolerance)
- Removes degenerate triangles
- Removes duplicate triangles
- Removes isolated vertices

### MeshHoleFilling.h
Detects and fills holes in meshes using GTE's robust triangulation:
- Finds boundary loops
- Choice of triangulation method:
  - **Ear Clipping (EC)**: Fast and simple
  - **Constrained Delaunay (CDT)**: Higher quality triangles
- Uses exact arithmetic (BSNumber) for robustness
- Supports area and edge count limits

**Example:**
```cpp
gte::MeshHoleFilling<double>::Parameters fillParams;
fillParams.maxArea = 1e30;
fillParams.method = gte::MeshHoleFilling<double>::TriangulationMethod::CDT;
gte::MeshHoleFilling<double>::FillHoles(vertices, triangles, fillParams);
```

### MeshPreprocessing.h
Mesh cleanup operations:
- Removes small facets
- Removes small connected components
- Orients normals consistently
- Inverts normals

## Building the Test Program

```bash
make
# Test with Ear Clipping
./test_mesh_repair input.obj output_ec.obj ec

# Test with Constrained Delaunay Triangulation
./test_mesh_repair input.obj output_cdt.obj cdt
```

## Documentation

See [IMPLEMENTATION_STATUS.md](IMPLEMENTATION_STATUS.md) for detailed documentation, API reference, and implementation notes.

## License

These headers are dual-licensed:
- Boost Software License 1.0 (GTE framework license)
- BSD 3-Clause (original Geogram code license)

Both licenses are permissive and compatible with BRL-CAD's LGPL 2.1.

## Credits

- **Original Algorithms:** Geogram by Bruno Levy and Inria
- **GTE Framework:** David Eberly, Geometric Tools
- **Port to GTE:** BRL-CAD Project

