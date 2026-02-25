# TODO

This file tracks known issues and improvements for the Geometric Tools Engine
(GTE), organized by priority.

## High Priority

### Documentation Typos

- [x] **README.md, line 19**: "provided,s" should be "provided," — stray `s`
  after the comma in "is provided,s GTMathematicsGPU."
- [x] **GTE/Mathematics/IntrEllipse2Ellipse2.h, line 14**: "robust.s The"
  should be "robust. The" — stray `s` after the period in the file's opening
  comment block.

## Medium Priority

### Code Refactoring (tracked TODOs in source)

- [ ] **GTE/Mathematics/SegmentMesh.h**: Allow for dynamic creation of meshes
  (noted as `TODO` in the source; currently only static mesh creation is
  supported).
- [ ] **GTE/Mathematics/AdaptiveSkeletonClimbing2.h**: Refactor the class so
  that it shares a common base class `CurveExtractor` with the 3-D analogue
  (noted as `TODO` in the source).
- [ ] **GTE/Mathematics/NearestNeighborQuery.h**: The implementation is not a
  true KD-tree; replace or rename to accurately reflect the algorithm used
  (noted as `TODO` in the source).

## Low Priority

### Performance Improvements (tracked TODOs in source)

- [ ] **GTE/Mathematics/NearestNeighborQuery.h**: Removing the final set of
  items from the internal heap can be made more efficient (noted as `TODO` in
  the source).
- [ ] **GTE/Mathematics/ConvexHull3.h**: A couple of potential algorithmic
  optimisations need to be explored (noted as `TODO` in the source).
