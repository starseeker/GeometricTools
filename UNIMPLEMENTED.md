# Unimplemented Geogram Features

---

### ⚠️ MINOR: Additional Optimization Methods

**Status:** Already have Lloyd and Newton/BFGS

**What's Implemented:**
- ✅ Lloyd relaxation (iterative centroid)
- ✅ Newton optimization with BFGS
- ✅ Basic line search

**What's Not Implemented:**
- ❌ Conjugate gradient variants
- ❌ L-BFGS (limited memory BFGS)
- ❌ Trust region methods

**Priority:** MEDIUM - let's be ready if convergence issues arise

### ⚠️ MINOR: Advanced Integration Utilities

**Status:** Basic integration complete

**What's Implemented:**
- ✅ Basic integration over triangular facets
- ✅ Centroid computation
- ✅ Mass computation

**What's Not Implemented:**
- ❌ High-order quadrature
- ❌ Moment computation beyond centroids

**Priority:** MEDIUM if potential for improved output or performance


### ⚠️ POTENTIAL: Parallel Processing Optimizations

**Status:** Basic threading implemented

**What's Implemented:**
- ✅ ThreadPool class
- ✅ Some parallel RVD computation

**What Could Be Enhanced:**
- Parallel RVD computation
- Full parallel Lloyd iterations

**Priority:** HIGH - large meshes are likely to be encountered at some point

### Other Minor Issues

1. **Code Comments to Update**
   - MeshAnisotropy.h line 59: Outdated TODO in comment block

2. **RVD Default Paths**
   - Need to verify which RVD version is used by default
   - RestrictedVoronoiDiagram vs RestrictedVoronoiDiagramOptimized

3. **MeshRemesh.h Bug**
   - test_anisotropic_remesh outputs 0 triangles
   - Bug in algorithm, NOT in anisotropic infrastructure
   - Anisotropic computation itself works correctly

4. **Code Cleanup**
   - Fix outdated comments
   - Update example code in comment blocks

**Priority:** N/A (completed with proper fix, not workaround)

### Update, remove, or consolidate all tests in tests/ for current setup

**What's Implemented:**
- A variety of tests built up over development - may contain outdated or OBE tests

**What Could Be Enhanced:**
- Remove unneeded/obsolete tests, make sure all valid ones work.  Enhance to address coverage of any issues like the MeshRemesh Bug

### Non-features

Geogram-specific infrastructure (options system, logging, etc.) should **not** be ported. Use GTE and BRL-CAD idioms instead.

