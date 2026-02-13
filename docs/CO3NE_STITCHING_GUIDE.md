# Co3Ne Manifold Patch Stitching System

## Overview

The Co3Ne algorithm produces high-quality manifold **patches** rather than a single fully-manifold mesh. This stitching system provides tools to combine these patches into a complete manifold mesh suitable for BRL-CAD integration.

## Problem Statement

Co3Ne reconstruction generates:
- Multiple disconnected manifold patches
- Patches that partially overlap but aren't properly connected
- "Floating" patches that need to be welded to neighbors
- Gaps between patches that need filling

**Example**: On a 1000-point sample from r768.xyz:
- Co3Ne produces: 170 triangles, 34 separate patches
- 4 closed patches (no boundaries)
- 30 open patches (with boundaries)
- 429 potential patch pairs within stitching distance
- Many patches share vertices but not triangles

## Architecture

### Core Components

#### 1. Co3NeManifoldStitcher

Main orchestration class that coordinates the stitching process.

**Location**: `GTE/Mathematics/Co3NeManifoldStitcher.h`

**Key Features**:
- **Topology Analysis**: Classifies edges as Interior, Boundary, or NonManifold
- **Patch Detection**: Identifies connected components via flood-fill
- **Patch Analysis**: Computes area, centroid, boundary status for each patch
- **Pair Analysis**: Identifies which patches are close enough to stitch
- **Non-manifold Removal**: Automatically removes triangles causing non-manifold edges

#### 2. Hole Filling Integration

Uses existing `MeshHoleFilling` class to fill gaps between patches.

**Strategy**:
- Detects boundary loops in patches
- Fills loops with configurable triangulation method (Ear Clipping, CDT, or 3D)
- Supports area and edge count limits for selective filling

**Current Status**: ✅ **Implemented and Tested**

#### 3. Ball Pivot Integration (In Progress)

Will use ball pivoting to "weld" floating patches together.

**Approach**:
- Use locally-tuned ball radii based on patch spacing
- Selectively apply ball pivot to gaps between specific patch pairs
- Preserve existing geometry while adding welding triangles

**Current Status**: 🔄 **Framework Created**

File: `GTE/Mathematics/BallPivotWrapper.h` (wrapper header)
Dependency: `BallPivot.hpp` (requires BRL-CAD libs: vmath.h, bu/log.h)

**Blockers**:
- BallPivot.hpp has BRL-CAD dependencies that need to be addressed
- Options:
  1. Create stub headers for vmath.h and bu/log.h in GTE context
  2. Refactor BallPivot.hpp to remove dependencies
  3. Use alternative triangulation approach

#### 4. UV Parameterization Alternative (Planned)

For complex cases where hole filling and ball pivot don't work well.

**Approach**:
- Parameterize neighboring patches to shared 2D space
- Identify outer boundary polygon in parameter space
- Retriangulate using detria.hpp with original interior vertices as Steiner points

**Current Status**: 📋 **Planned**

## Usage

### Basic Example

```cpp
#include <GTE/Mathematics/Co3Ne.h>
#include <GTE/Mathematics/Co3NeManifoldStitcher.h>

// Step 1: Run Co3Ne reconstruction
std::vector<Vector3<double>> points = LoadPointCloud("input.xyz");

Co3Ne<double>::Parameters co3neParams;
co3neParams.relaxedManifoldExtraction = true;

std::vector<Vector3<double>> vertices;
std::vector<std::array<int32_t, 3>> triangles;

Co3Ne<double>::Reconstruct(points, vertices, triangles, co3neParams);

// Step 2: Stitch patches together
Co3NeManifoldStitcher<double>::Parameters stitchParams;
stitchParams.verbose = true;
stitchParams.enableHoleFilling = true;
stitchParams.removeNonManifoldEdges = true;
stitchParams.maxHoleEdges = 50;

bool isManifold = Co3NeManifoldStitcher<double>::StitchPatches(
    vertices, triangles, stitchParams);
```

### Configuration Parameters

```cpp
struct Parameters
{
    // Hole filling
    bool enableHoleFilling;        // Enable hole filling step
    Real maxHoleArea;               // Maximum hole area to fill (0 = no limit)
    size_t maxHoleEdges;            // Maximum hole perimeter edges
    TriangulationMethod holeFillingMethod;  // EC, CDT, or EarClipping3D
    
    // Ball pivot (when implemented)
    bool enableBallPivot;           // Enable ball pivot stitching
    std::vector<Real> ballRadii;    // Radii to try (auto if empty)
    
    // UV merging (when implemented)
    bool enableUVMerging;           // Enable UV parameterization approach
    
    // General
    bool removeNonManifoldEdges;    // Remove triangles causing non-manifold edges
    bool verbose;                   // Enable detailed progress output
};
```

## Test Suite

### Running Tests

```bash
make test_co3ne_stitcher
./test_co3ne_stitcher 1000  # Test with 1000 points from r768.xyz
```

### Test Coverage

1. **Basic Stitching**: Simple cube point cloud
2. **Non-Manifold Removal**: Synthetic non-manifold edge case
3. **Real Data**: r768.xyz point cloud with varying point counts

### Example Output

```
=== Test: Real Data Stitching (r768.xyz) ===
Loaded 1000 points
Running Co3Ne reconstruction...
Co3Ne reconstruction: SUCCESS
Generated 170 triangles from 1000 vertices

Topology Analysis:
  Boundary edges: 156
  Interior edges: 174
  Non-manifold edges: 2
Removed 2 triangles to fix non-manifold edges

Found 34 connected patches
  4 closed patches (no boundaries)
  30 open patches (have boundaries)

Found 429 patch pairs for potential stitching
  Pair 0: patches 2 and 33, min dist = 0, close vertices = 23
  Pair 1: patches 5 and 14, min dist = 0, close vertices = 32
  ...

Hole filling added 10 triangles
Final mesh: 178 triangles, 144 vertices
```

## Algorithm Details

### Patch Detection

Uses flood-fill on triangle adjacency graph:

1. Build edge-to-triangle map
2. Build triangle adjacency (triangles sharing edges)
3. Flood-fill from each unvisited triangle
4. Each connected component = one patch

### Patch Pair Analysis

Identifies which patches should be stitched:

1. Compute patch centroids for quick distance filtering
2. For promising pairs:
   - Extract boundary vertices from both patches
   - Compute min/max distances between boundaries
   - Count vertices within stitching threshold
3. Sort pairs by minimum distance (stitch closest first)

### Non-Manifold Edge Removal

Greedy algorithm to ensure manifold topology:

1. Detect edges shared by 3+ triangles
2. For each non-manifold edge:
   - Keep first 2 triangles
   - Remove remaining triangles
3. Guarantees manifold but may create gaps

### Hole Filling

Uses existing `MeshHoleFilling` infrastructure:

1. Detect boundary loops via edge traversal
2. Filter by size constraints (area, edge count)
3. Triangulate using selected method:
   - **Ear Clipping**: Fast, good for simple holes
   - **CDT**: Robust, handles complex cases
   - **EarClipping3D**: For non-planar holes
4. Add new triangles to mesh
5. Optional: Run mesh repair

## Current Limitations

### 1. Ball Pivot Dependency Issue

**Problem**: BallPivot.hpp requires BRL-CAD headers (vmath.h, bu/log.h)

**Impact**: Cannot directly use ball pivot in pure GTE context

**Solutions**:
- **Short-term**: Create minimal stub headers
- **Long-term**: Extract ball pivot algorithm into pure GTE implementation

### 2. Disconnected Patches

**Problem**: Hole filling works well for gaps within a patch, but doesn't connect separate patches

**Impact**: Multiple disconnected components remain

**Solution**: Need ball pivot or UV merging to join patches

### 3. Over-Conservative Non-Manifold Removal

**Problem**: Greedy removal may remove more triangles than necessary

**Impact**: Can create new gaps that need filling

**Solution**: Implement smarter selection (e.g., remove triangles with worst quality)

## Future Enhancements

### Phase 1: Complete Ball Pivot Integration

**Tasks**:
1. ✅ Create BallPivotWrapper.h interface
2. ⏳ Resolve BRL-CAD dependency issue
3. ⏳ Implement local ball pivot for patch pairs
4. ⏳ Add adaptive radius selection based on patch spacing
5. ⏳ Test on real data

**Expected Benefit**: Join floating patches that share no vertices

### Phase 2: UV Parameterization

**Tasks**:
1. ⏳ Implement LSCM or ABF++ parameterization for patches
2. ⏳ Map neighboring patch pairs to shared 2D space
3. ⏳ Identify outer boundary in parameter space
4. ⏳ Integrate detria.hpp for 2D Delaunay triangulation
5. ⏳ Lift result back to 3D

**Expected Benefit**: Handle complex gaps that neither hole filling nor ball pivot can solve

### Phase 3: Iterative Stitching

**Tasks**:
1. ⏳ Implement progressive stitching loop
2. ⏳ Stitch closest patch pairs first
3. ⏳ Re-analyze after each stitch
4. ⏳ Continue until no more pairs within threshold

**Expected Benefit**: Maximize connectivity while preserving manifold property

### Phase 4: Quality Metrics

**Tasks**:
1. ⏳ Add triangle quality measurement
2. ⏳ Prefer stitching methods that preserve quality
3. ⏳ Reject stitches that create poor triangles
4. ⏳ Add mesh smoothing post-process

**Expected Benefit**: Ensure stitched mesh meets quality standards

## Integration with BRL-CAD

### Recommended Workflow

```cpp
// 1. Run Co3Ne with relaxed mode for better coverage
Co3Ne<double>::Parameters co3neParams;
co3neParams.relaxedManifoldExtraction = true;
co3neParams.kNeighbors = 20;

Co3Ne<double>::Reconstruct(points, vertices, triangles, co3neParams);

// 2. Stitch patches with hole filling
Co3NeManifoldStitcher<double>::Parameters stitchParams;
stitchParams.enableHoleFilling = true;
stitchParams.maxHoleEdges = 50;  // Adjust based on point density
stitchParams.removeNonManifoldEdges = true;

Co3NeManifoldStitcher<double>::StitchPatches(vertices, triangles, stitchParams);

// 3. Validate and repair
MeshRepair<double>::Parameters repairParams;
MeshRepair<double>::Repair(vertices, triangles, repairParams);

// 4. Verify manifold property
bool isManifold = MeshValidation<double>::IsManifold(triangles);
```

### Expected Results

| Metric | Before Stitching | After Stitching |
|--------|------------------|-----------------|
| Triangles | 170 | 178 (+4.7%) |
| Patches | 34 | ~10-15 (estimated) |
| Non-manifold edges | 2 | 0 (guaranteed) |
| Closed patches | 4 | More (via hole filling) |
| Coverage | Sparse | Better |

## Performance Characteristics

### Time Complexity

- **Topology Analysis**: O(T) where T = triangle count
- **Patch Detection**: O(T) via flood-fill
- **Patch Pair Analysis**: O(P²×B²) where P = patch count, B = avg boundary vertices
- **Hole Filling**: O(H×E²) where H = hole count, E = avg hole edges

### Space Complexity

- O(T + V + P) where V = vertex count, P = patch count

### Typical Performance

On r768.xyz with 1000 points:
- Co3Ne reconstruction: ~1-2 seconds
- Stitching analysis: <0.1 seconds
- Hole filling: <0.1 seconds
- Total: ~1-2 seconds

## References

### Papers

1. **Co3Ne**: "Co-Cones: Co-Cones and Concurrent Co-Cones" (Geogram implementation)
2. **Ball Pivoting**: "The Ball-Pivoting Algorithm for Surface Reconstruction" (Open3D implementation)
3. **Constrained Delaunay**: Various GTE triangulation algorithms

### Code

- `GTE/Mathematics/Co3Ne.h` - Main Co3Ne reconstruction
- `GTE/Mathematics/Co3NeManifoldStitcher.h` - This stitching system
- `GTE/Mathematics/MeshHoleFilling.h` - Hole filling implementation
- `BallPivot.hpp` - Ball pivot implementation (external)
- `detria.hpp` - 2D Delaunay triangulation (external)

## Contributing

When adding new stitching strategies:

1. Add method to `Co3NeManifoldStitcher` class
2. Add corresponding parameters to `Parameters` struct
3. Add test case to `test_co3ne_stitcher.cpp`
4. Update this documentation
5. Follow GTE coding style

## License

This implementation follows GTE's Boost Software License 1.0.

## Authors

- Ported from Geogram's Co3Ne implementation
- BRL-CAD integration work
- GTE-style adaptation
