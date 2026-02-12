# Anisotropic Remeshing Test

This test program demonstrates the anisotropic remeshing infrastructure implemented for GTE.

## Building

```bash
make test_anisotropic_remesh
```

## Usage

```bash
./test_anisotropic_remesh input.obj output.obj [anisotropy_scale]
```

### Parameters

- `input.obj` - Input mesh file (Wavefront OBJ format)
- `output.obj` - Output remeshed file
- `anisotropy_scale` - (Optional) Anisotropy scale factor (default: 0.04)
  - `0.0` = Isotropic (uniform) remeshing
  - `0.02-0.1` = Anisotropic remeshing (typical range)
  - Higher values = more anisotropy (more directional adaptation)

## What This Tests

The test program validates:

1. **Basic Anisotropy Computation**
   - Computes vertex normals from mesh
   - Scales normals by anisotropy factor
   - Validates normal computation

2. **6D Point Infrastructure**
   - Creates 6D points: (x, y, z, nx*s, ny*s, nz*s)
   - Extracts 3D positions from 6D points
   - Extracts 3D normals from 6D points

3. **Curvature-Adaptive Anisotropy**
   - Uses GTE's MeshCurvature to compute principal curvatures
   - Adapts anisotropy scale based on local curvature
   - Higher curvature = more anisotropic adaptation

4. **Integration with Remeshing**
   - Tests parameter passing to MeshRemeshFull
   - Validates anisotropic mode flag
   - Verifies remeshing completes successfully

## Example Output

```
$ ./test_anisotropic_remesh input.obj output.obj 0.04

Loading mesh from input.obj...
Input mesh: 1000 vertices, 1998 triangles

Testing anisotropy computation...
Computing anisotropic normals with scale = 0.04
Computed 1000 scaled normals
Created 1000 points in 6D space
Extracted 1000 positions and 1000 normals

Testing anisotropic remeshing...
Remeshing with parameters:
  Target vertices: 1000
  Lloyd iterations: 5
  Use RVD: yes
  Anisotropic: yes
  Anisotropy scale: 0.04

Output mesh: 1000 vertices, 1998 triangles

Saving result to output.obj...
Success!

Testing curvature-adaptive anisotropy...
Computed 1000 curvature-adaptive normals
```

## Understanding Anisotropy Scale

The anisotropy scale determines how much the mesh adapts to surface features:

- **0.0** - Isotropic mode (no directional adaptation)
  - Mesh elements are uniformly sized
  - Good for uniform surfaces

- **0.02-0.04** - Mild anisotropy (recommended for most cases)
  - Moderate adaptation to curvature
  - Balances quality and triangle count

- **0.05-0.1** - Strong anisotropy
  - Aggressive adaptation to features
  - Can reduce triangle count significantly
  - May create elongated triangles

The scale is relative to the mesh bounding box diagonal, so the same value works across different mesh scales.

## Current Implementation Status

### ✅ Implemented
- All anisotropic utility functions (MeshAnisotropy.h)
- Parameter support in MeshRemeshFull
- 6D point creation and extraction
- Curvature-adaptive scaling
- Test framework

### 🔮 Future Enhancement
Full 6D CVT anisotropic remeshing requires extending GTE's Delaunay3 to support arbitrary dimensions (currently 3D only). This would enable:
- True 6D distance metrics in Voronoi computation
- Anisotropic Voronoi cells
- Complete geogram parity

See `docs/ANISOTROPIC_REMESHING.md` for details on the full implementation path.

## Related Files

- `GTE/Mathematics/MeshAnisotropy.h` - Anisotropic utilities
- `GTE/Mathematics/MeshRemeshFull.h` - Remeshing with anisotropic support
- `GTE/Mathematics/MeshCurvature.h` - Curvature computation
- `docs/ANISOTROPIC_REMESHING.md` - Complete documentation

## References

- Geogram anisotropic remeshing: `geogram/src/lib/geogram/mesh/mesh_remesh.h`
- Usage example in geogram: `set_anisotropy(M, 0.04); remesh_smooth(M, M_out, 30000, 6);`
