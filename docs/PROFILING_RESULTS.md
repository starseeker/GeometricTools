# Profiling Results - Co3Ne Manifold Stitching

## Test Setup

**Input**: 1000 points from r768.xyz  
**Co3Ne Parameters**:
- kNeighbors = 20
- relaxedManifoldExtraction = true
- orientNormals = true

**Co3Ne Output**: 428 triangles, 102 components

## High-Level Timing

```
Operation                    Time (s)    %
--------------------------------------------
Co3Ne Reconstruction         0.988      6.7%
Manifold Stitching          13.761     93.3%
Total                       14.749    100.0%
```

**Finding**: Stitching dominates (93%), as expected.

## Stitching Breakdown (from verbose output)

### Phase 1: Initial Topology Analysis
- Remove non-manifold edges: 7 triangles removed
- Identify patches: 102 components found
- Analyze patch pairs: Many pairs evaluated
- Hole filling (first pass): Added triangles

### Phase 2: Iterative Component Bridging

**6 Iterations** with increasing thresholds:
1. Threshold 82.1 (2x edge length)
2. Threshold 82.1 (2x)  
3. Threshold 123.2 (3x)
4. Threshold 184.7 (4.5x)
5. Threshold 231.9 (5.65x)
6. Threshold 277.1 (6.75x)

Each iteration performs:
- Extract components (O(T))
- Incremental component merging:
  - Multiple sub-iterations
  - Find closest component per component
  - Find candidate edge pairs
  - Attempt bridges
  - Update component structures

### Phase 3: Hole Filling
- Filled 17 holes total
- Capped 5 boundary loops

## Estimated Time Distribution

Based on algorithm complexity and iteration counts:

```
Operation                           Estimated Time    %
---------------------------------------------------------
Component Extraction (6 iterations)      ~4-6s      30-45%
Candidate Edge Finding                   ~3-5s      20-35%
Bridge Validation/Creation               ~2-4s      15-30%
Hole Filling                             ~1-2s       8-15%
Other (topology analysis, etc.)          ~0.5-1s     5-10%
```

## Key Bottlenecks Identified

### 1. Component Extraction (Repeated)
**Problem**: Called 6 times (once per iteration), each time O(T)
- Build edge-to-triangle map
- BFS traversal to find components
- Extract boundary edges per component
- Compute centroids and bounding spheres

**Cost**: With 421-699 triangles, ~4-6 seconds total

### 2. Candidate Edge Pair Finding
**Problem**: For each component pair, O(B1 × B2) comparisons
- 102 initial components, many with 3-13 boundary edges
- Many component pairs considered
- Distance computation for each edge pair
- Even with spatial indexing, significant overhead

**Cost**: ~3-5 seconds across all iterations

### 3. Bridge Validation
**Problem**: Each bridge attempt requires extensive validation
- Check manifold properties
- Verify orientations
- Detect duplicates
- Update mesh structures

**Cost**: ~2-4 seconds for all bridge attempts

### 4. Multiple Iterations
**Problem**: Need 6 iterations with increasing thresholds
- Each iteration extracts components from scratch
- Early iterations create few bridges
- Later iterations often find no valid bridges

**Cost**: Multiplicative effect on all per-iteration costs

## Profiling Limitations

Without kernel-level profiling (perf not available), estimates are based on:
- Algorithm complexity analysis
- Verbose output observations
- High-level timing measurements
- Experience with similar code patterns

## Recommendations for Optimization

### High Priority: Reduce Component Extraction Cost

**Current**: O(T) extraction × 6 iterations = O(6T)

**Options**:
1. **Incremental extraction** (already attempted, limited success)
   - Update component membership without full re-scan
   - Complexity: High implementation, moderate gains

2. **Cache between iterations** 
   - Store component data structures
   - Invalidate only affected components
   - Complexity: Medium, good potential

3. **Reduce iterations**
   - More aggressive bridging per iteration
   - Accept sub-optimal topology
   - Complexity: Low, immediate gains

### Medium Priority: Optimize Edge Pair Finding

**Current**: O(B²) for each component pair

**Options**:
1. **Better spatial indexing**
   - Current implementation helps but threshold limits effectiveness
   - Could use more sophisticated BVH
   - Complexity: Medium

2. **Caching edge pair candidates**
   - Store valid pairs between iterations
   - Only recompute for modified components
   - Complexity: Medium

### Low Priority: Parallel Processing

Most operations are parallelizable:
- Component extraction (independent BFS)
- Edge pair finding (independent comparisons)
- Multiple bridge attempts

Expected: 2-4x speedup on multi-core

## Next Steps for Detailed Profiling

To get exact measurements, need one of:

1. **Manual instrumentation**
   - Add timers to each function
   - Compile with instrumentation
   - Run and collect data

2. **Sampling profiler** (if perf becomes available)
   - perf record -g
   - perf report
   - Shows exact function time distribution

3. **Compiler instrumentation**
   - -finstrument-functions
   - Generate call graph with timing

4. **gprof** (if available)
   - Compile with -pg
   - Run and generate gmon.out
   - Analyze with gprof

---

*Report generated: 2026-02-19*
*Based on manual timing and algorithm analysis*
