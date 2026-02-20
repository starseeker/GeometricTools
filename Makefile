# Makefile for building GTE mesh processing test programs

CXX = g++
CXXFLAGS = -std=c++17 -O2 -Wall -I. -IGTE
LDFLAGS = 
PTHREAD = -pthread

# Test source directory
TEST_DIR = tests

# Primary test targets
TARGETS = test_mesh_repair test_co3ne test_full_algorithms test_rvd \
          demo_rvd_cvt test_remesh_comparison test_co3ne_rvd test_newton_optimizer \
          test_rvd_performance stress_test test_threadpool test_parallel_rvd \
          test_enhanced_manifold test_anisotropic_remesh test_delaunay6 test_cvt6d \
          test_delaunay_n test_delaunay_nn test_rvd_n test_cvt_n test_phase4_integration \
          test_anisotropic_end_to_end test_co3ne_stitcher test_ball_pivot_hole_filler \
          test_ball_pivot_integration test_comprehensive_manifold_analysis test_progressive_merging \
          test_parallel_nntree_queries test_patch_cluster_merger

all: $(TARGETS)

# Basic functionality tests
test_mesh_repair: $(TEST_DIR)/test_mesh_repair.cpp
	$(CXX) $(CXXFLAGS) -o test_mesh_repair $(TEST_DIR)/test_mesh_repair.cpp $(LDFLAGS)

test_remesh: $(TEST_DIR)/test_remesh.cpp
	$(CXX) $(CXXFLAGS) -o test_remesh $(TEST_DIR)/test_remesh.cpp $(LDFLAGS)

test_co3ne: $(TEST_DIR)/test_co3ne.cpp
	$(CXX) $(CXXFLAGS) -o test_co3ne $(TEST_DIR)/test_co3ne.cpp $(LDFLAGS)

test_full_algorithms: $(TEST_DIR)/test_full_algorithms.cpp
	$(CXX) $(CXXFLAGS) -o test_full_algorithms $(TEST_DIR)/test_full_algorithms.cpp $(LDFLAGS)

test_rvd: $(TEST_DIR)/test_rvd.cpp
	$(CXX) $(CXXFLAGS) -o test_rvd $(TEST_DIR)/test_rvd.cpp $(LDFLAGS)

# Comparison and validation
test_remesh_comparison: $(TEST_DIR)/test_remesh_comparison.cpp
	$(CXX) $(CXXFLAGS) -o test_remesh_comparison $(TEST_DIR)/test_remesh_comparison.cpp $(LDFLAGS)

test_co3ne_rvd: $(TEST_DIR)/test_co3ne_rvd.cpp
	$(CXX) $(CXXFLAGS) -o test_co3ne_rvd $(TEST_DIR)/test_co3ne_rvd.cpp $(LDFLAGS)

# Performance and optimization
test_newton_optimizer: $(TEST_DIR)/test_newton_optimizer.cpp
	$(CXX) $(CXXFLAGS) -o test_newton_optimizer $(TEST_DIR)/test_newton_optimizer.cpp $(LDFLAGS)

test_rvd_performance: $(TEST_DIR)/test_rvd_performance.cpp
	$(CXX) $(CXXFLAGS) -o test_rvd_performance $(TEST_DIR)/test_rvd_performance.cpp $(LDFLAGS)

# Stress testing
stress_test: $(TEST_DIR)/stress_test.cpp
	$(CXX) $(CXXFLAGS) -o stress_test $(TEST_DIR)/stress_test.cpp $(LDFLAGS)

# Parallel processing tests
test_threadpool: $(TEST_DIR)/test_threadpool.cpp GTE/Mathematics/ThreadPool.h
	$(CXX) $(CXXFLAGS) $(PTHREAD) -o test_threadpool $(TEST_DIR)/test_threadpool.cpp $(LDFLAGS)

test_parallel_rvd: $(TEST_DIR)/test_parallel_rvd.cpp GTE/Mathematics/RestrictedVoronoiDiagramOptimized.h GTE/Mathematics/ThreadPool.h
	$(CXX) $(CXXFLAGS) $(PTHREAD) -o test_parallel_rvd $(TEST_DIR)/test_parallel_rvd.cpp $(LDFLAGS)

test_enhanced_manifold: $(TEST_DIR)/test_enhanced_manifold.cpp
	$(CXX) $(CXXFLAGS) $(PTHREAD) -o test_enhanced_manifold $(TEST_DIR)/test_enhanced_manifold.cpp $(LDFLAGS)

# Anisotropic remeshing test
test_anisotropic_remesh: $(TEST_DIR)/test_anisotropic_remesh.cpp GTE/Mathematics/MeshAnisotropy.h GTE/Mathematics/MeshRemesh.h
	$(CXX) $(CXXFLAGS) -o test_anisotropic_remesh $(TEST_DIR)/test_anisotropic_remesh.cpp $(LDFLAGS)

# 6D Delaunay test
test_delaunay6: $(TEST_DIR)/test_delaunay6.cpp GTE/Mathematics/Delaunay6.h GTE/Mathematics/MeshAnisotropy.h
	$(CXX) $(CXXFLAGS) -o test_delaunay6 $(TEST_DIR)/test_delaunay6.cpp $(LDFLAGS)

# 6D CVT test
test_cvt6d: $(TEST_DIR)/test_cvt6d.cpp GTE/Mathematics/CVT6D.h GTE/Mathematics/MeshAnisotropy.h
	$(CXX) $(CXXFLAGS) -o test_cvt6d $(TEST_DIR)/test_cvt6d.cpp $(LDFLAGS)

# DelaunayN base class test
test_delaunay_n: $(TEST_DIR)/test_delaunay_n.cpp GTE/Mathematics/DelaunayN.h
	$(CXX) $(CXXFLAGS) -o test_delaunay_n $(TEST_DIR)/test_delaunay_n.cpp $(LDFLAGS)

# DelaunayNN implementation test
test_delaunay_nn: $(TEST_DIR)/test_delaunay_nn.cpp GTE/Mathematics/DelaunayNN.h GTE/Mathematics/NearestNeighborSearchN.h GTE/Mathematics/DelaunayN.h
	$(CXX) $(CXXFLAGS) -o test_delaunay_nn $(TEST_DIR)/test_delaunay_nn.cpp $(LDFLAGS)

# RestrictedVoronoiDiagramN test
test_rvd_n: $(TEST_DIR)/test_rvd_n.cpp GTE/Mathematics/RestrictedVoronoiDiagramN.h GTE/Mathematics/DelaunayNN.h GTE/Mathematics/MeshAnisotropy.h
	$(CXX) $(CXXFLAGS) -o test_rvd_n $(TEST_DIR)/test_rvd_n.cpp $(LDFLAGS)

# CVTN (N-dimensional CVT) test
test_cvt_n: $(TEST_DIR)/test_cvt_n.cpp GTE/Mathematics/CVTN.h GTE/Mathematics/RestrictedVoronoiDiagramN.h GTE/Mathematics/DelaunayNN.h GTE/Mathematics/MeshAnisotropy.h
	$(CXX) $(CXXFLAGS) -o test_cvt_n $(TEST_DIR)/test_cvt_n.cpp $(LDFLAGS)

# Demonstration programs
demo_rvd_cvt: $(TEST_DIR)/demo_rvd_cvt.cpp
	$(CXX) $(CXXFLAGS) -o demo_rvd_cvt $(TEST_DIR)/demo_rvd_cvt.cpp $(LDFLAGS)

# Test data generation
generate_test_data:
	@echo "Generating stress test meshes..."
	python3 $(TEST_DIR)/scripts/create_stress_meshes.py
	@echo "Test data generated in $(TEST_DIR)/data/"

# Run tests
test: test_mesh_repair
	@echo "Running basic mesh repair test..."
	./test_mesh_repair $(TEST_DIR)/data/gt.obj $(TEST_DIR)/data/gt_repaired.obj

stress: stress_test
	@echo "Running comprehensive stress tests..."
	./stress_test

# Clean up
clean:
	rm -f $(TARGETS)
	rm -f $(TEST_DIR)/data/*_repaired.obj $(TEST_DIR)/data/*_output.obj

.PHONY: all clean test stress generate_test_data

# Phase 4 integration test
test_phase4_integration: $(TEST_DIR)/test_phase4_integration.cpp GTE/Mathematics/CVTN.h GTE/Mathematics/MeshRemesh.h
	$(CXX) $(CXXFLAGS) -o test_phase4_integration $(TEST_DIR)/test_phase4_integration.cpp $(LDFLAGS)

# Comprehensive anisotropic remeshing end-to-end test
test_anisotropic_end_to_end: $(TEST_DIR)/test_anisotropic_end_to_end.cpp GTE/Mathematics/MeshRemesh.h GTE/Mathematics/CVTN.h GTE/Mathematics/MeshAnisotropy.h
	$(CXX) $(CXXFLAGS) -o test_anisotropic_end_to_end $(TEST_DIR)/test_anisotropic_end_to_end.cpp $(LDFLAGS)

# Co3Ne Manifold Stitcher test
test_co3ne_stitcher: $(TEST_DIR)/test_co3ne_stitcher.cpp GTE/Mathematics/Co3NeManifoldStitcher.h GTE/Mathematics/Co3Ne.h GTE/Mathematics/MeshHoleFilling.h GTE/Mathematics/BallPivotReconstruction.h GTE/Mathematics/BallPivotReconstruction.cpp GTE/Mathematics/BallPivotMeshHoleFiller.cpp
	$(CXX) $(CXXFLAGS) -o test_co3ne_stitcher $(TEST_DIR)/test_co3ne_stitcher.cpp GTE/Mathematics/BallPivotReconstruction.cpp GTE/Mathematics/BallPivotMeshHoleFiller.cpp $(LDFLAGS)

# Topology-aware bridging test
test_topology: $(TEST_DIR)/test_topology_bridging.cpp GTE/Mathematics/Co3NeManifoldStitcher.h GTE/Mathematics/Co3Ne.h GTE/Mathematics/BallPivotReconstruction.h GTE/Mathematics/BallPivotReconstruction.cpp
	$(CXX) $(CXXFLAGS) -o test_topology $(TEST_DIR)/test_topology_bridging.cpp GTE/Mathematics/BallPivotReconstruction.cpp $(LDFLAGS)

# Large input performance test
test_large: $(TEST_DIR)/test_large_input.cpp GTE/Mathematics/Co3NeManifoldStitcher.h GTE/Mathematics/Co3Ne.h
	$(CXX) $(CXXFLAGS) -o test_large $(TEST_DIR)/test_large_input.cpp GTE/Mathematics/BallPivotReconstruction.cpp $(LDFLAGS)

# Hybrid Co3Ne + Poisson reconstruction test (requires PoissonRecon submodule)
POISSON_INC = -I./PoissonRecon/Src
POISSON_FLAGS = -fopenmp -Wno-deprecated -pthread
test_hybrid: $(TEST_DIR)/test_hybrid_reconstruction.cpp GTE/Mathematics/HybridReconstruction.h GTE/Mathematics/PoissonWrapper.h GTE/Mathematics/Co3Ne.h
	$(CXX) $(CXXFLAGS) $(POISSON_INC) $(POISSON_FLAGS) -o test_hybrid $(TEST_DIR)/test_hybrid_reconstruction.cpp GTE/Mathematics/BallPivotReconstruction.cpp $(LDFLAGS) -lgomp

# Hybrid validation test - comprehensive validation of all merge strategies
test_hybrid_validation: $(TEST_DIR)/test_hybrid_validation.cpp GTE/Mathematics/HybridReconstruction.h GTE/Mathematics/PoissonWrapper.h GTE/Mathematics/Co3Ne.h
	$(CXX) $(CXXFLAGS) $(POISSON_INC) $(POISSON_FLAGS) -o test_hybrid_validation $(TEST_DIR)/test_hybrid_validation.cpp GTE/Mathematics/BallPivotReconstruction.cpp $(LDFLAGS) -lgomp

# Ball Pivot Mesh Hole Filler test
test_ball_pivot_hole_filler: $(TEST_DIR)/test_ball_pivot_hole_filler.cpp GTE/Mathematics/BallPivotMeshHoleFiller.h GTE/Mathematics/BallPivotMeshHoleFiller.cpp
	$(CXX) $(CXXFLAGS) -o test_ball_pivot_hole_filler $(TEST_DIR)/test_ball_pivot_hole_filler.cpp GTE/Mathematics/BallPivotMeshHoleFiller.cpp $(LDFLAGS)

# Ball Pivot Integration test (with Co3Ne)
test_ball_pivot_integration: $(TEST_DIR)/test_ball_pivot_integration.cpp GTE/Mathematics/BallPivotMeshHoleFiller.h GTE/Mathematics/BallPivotMeshHoleFiller.cpp GTE/Mathematics/Co3NeManifoldStitcher.h GTE/Mathematics/Co3Ne.h
	$(CXX) $(CXXFLAGS) -o test_ball_pivot_integration $(TEST_DIR)/test_ball_pivot_integration.cpp GTE/Mathematics/BallPivotMeshHoleFiller.cpp GTE/Mathematics/BallPivotReconstruction.cpp $(LDFLAGS)

# Iterative component bridging test
test_iterative_bridging: $(TEST_DIR)/test_iterative_bridging.cpp GTE/Mathematics/BallPivotMeshHoleFiller.h GTE/Mathematics/BallPivotMeshHoleFiller.cpp GTE/Mathematics/Co3Ne.h
	$(CXX) $(CXXFLAGS) -o test_iterative_bridging $(TEST_DIR)/test_iterative_bridging.cpp GTE/Mathematics/BallPivotMeshHoleFiller.cpp GTE/Mathematics/BallPivotReconstruction.cpp $(LDFLAGS)

# Manifold Closure test with r768.xyz
test_manifold_closure_r768: $(TEST_DIR)/test_manifold_closure_r768.cpp GTE/Mathematics/BallPivotMeshHoleFiller.h GTE/Mathematics/BallPivotMeshHoleFiller.cpp GTE/Mathematics/Co3NeManifoldStitcher.h GTE/Mathematics/Co3Ne.h
	$(CXX) $(CXXFLAGS) -o test_manifold_closure_r768 $(TEST_DIR)/test_manifold_closure_r768.cpp GTE/Mathematics/BallPivotMeshHoleFiller.cpp GTE/Mathematics/BallPivotReconstruction.cpp $(LDFLAGS)

# Comprehensive manifold analysis tool
test_comprehensive_manifold_analysis: $(TEST_DIR)/test_comprehensive_manifold_analysis.cpp GTE/Mathematics/BallPivotMeshHoleFiller.h GTE/Mathematics/BallPivotMeshHoleFiller.cpp GTE/Mathematics/Co3Ne.h
	$(CXX) $(CXXFLAGS) -o test_comprehensive_manifold_analysis $(TEST_DIR)/test_comprehensive_manifold_analysis.cpp GTE/Mathematics/BallPivotMeshHoleFiller.cpp GTE/Mathematics/BallPivotReconstruction.cpp $(LDFLAGS)


# UV unwrapping assessment test
test_uv_unwrapping_assessment: $(TEST_DIR)/test_uv_unwrapping_assessment.cpp GTE/Mathematics/BallPivotMeshHoleFiller.h GTE/Mathematics/BallPivotMeshHoleFiller.cpp GTE/Mathematics/LSCMParameterization.h GTE/Mathematics/Co3Ne.h
	$(CXX) $(CXXFLAGS) -o test_uv_unwrapping_assessment $(TEST_DIR)/test_uv_unwrapping_assessment.cpp GTE/Mathematics/BallPivotMeshHoleFiller.cpp GTE/Mathematics/BallPivotReconstruction.cpp $(LDFLAGS)


# Manifold Production Pipeline test - comprehensive pipeline with UV unwrapping
test_manifold_production_pipeline: $(TEST_DIR)/test_manifold_production_pipeline.cpp GTE/Mathematics/ManifoldProductionPipeline.h GTE/Mathematics/BallPivotMeshHoleFiller.h GTE/Mathematics/BallPivotMeshHoleFiller.cpp GTE/Mathematics/LSCMParameterization.h GTE/Mathematics/Co3Ne.h
	$(CXX) $(CXXFLAGS) -o test_manifold_production_pipeline $(TEST_DIR)/test_manifold_production_pipeline.cpp GTE/Mathematics/BallPivotMeshHoleFiller.cpp GTE/Mathematics/BallPivotReconstruction.cpp $(LDFLAGS)

# Progressive component merging test
test_progressive_merging: $(TEST_DIR)/test_progressive_merging.cpp GTE/Mathematics/Co3NeManifoldStitcher.h GTE/Mathematics/BallPivotReconstruction.cpp GTE/Mathematics/BallPivotMeshHoleFiller.cpp
	$(CXX) $(CXXFLAGS) -o test_progressive_merging $(TEST_DIR)/test_progressive_merging.cpp GTE/Mathematics/BallPivotReconstruction.cpp GTE/Mathematics/BallPivotMeshHoleFiller.cpp $(LDFLAGS)

# Bridging comparison analysis
test_bridging_comparison: $(TEST_DIR)/test_bridging_comparison.cpp GTE/Mathematics/Co3NeManifoldStitcher.h
	$(CXX) $(CXXFLAGS) -o test_bridging_comparison $(TEST_DIR)/test_bridging_comparison.cpp $(LDFLAGS)

# BoundaryPolygonRTree correctness and performance test
test_boundary_polygon_rtree: $(TEST_DIR)/test_boundary_polygon_rtree.cpp GTE/Mathematics/BoundaryPolygonRTree.h
	$(CXX) $(CXXFLAGS) -o test_boundary_polygon_rtree $(TEST_DIR)/test_boundary_polygon_rtree.cpp $(LDFLAGS)

# Performance benchmark test
test_performance_benchmark: $(TEST_DIR)/test_performance_benchmark.cpp GTE/Mathematics/Co3NeManifoldStitcher.h GTE/Mathematics/Co3Ne.h GTE/Mathematics/BallPivotReconstruction.cpp GTE/Mathematics/BallPivotMeshHoleFiller.cpp
	$(CXX) $(CXXFLAGS) -o test_performance_benchmark $(TEST_DIR)/test_performance_benchmark.cpp GTE/Mathematics/BallPivotReconstruction.cpp GTE/Mathematics/BallPivotMeshHoleFiller.cpp $(LDFLAGS)

# Bottleneck analysis test
test_bottleneck: $(TEST_DIR)/test_bottleneck.cpp GTE/Mathematics/Co3NeManifoldStitcher.h GTE/Mathematics/Co3Ne.h GTE/Mathematics/BallPivotReconstruction.cpp GTE/Mathematics/BallPivotMeshHoleFiller.cpp
	$(CXX) $(CXXFLAGS) -o test_bottleneck $(TEST_DIR)/test_bottleneck.cpp GTE/Mathematics/BallPivotReconstruction.cpp GTE/Mathematics/BallPivotMeshHoleFiller.cpp $(LDFLAGS)

# Parallel NNTree query correctness test
test_parallel_nntree_queries: $(TEST_DIR)/test_parallel_nntree_queries.cpp GTE/Mathematics/Co3NeManifoldStitcher.h GTE/Mathematics/BallPivotMeshHoleFiller.cpp
	$(CXX) $(CXXFLAGS) $(PTHREAD) -o test_parallel_nntree_queries $(TEST_DIR)/test_parallel_nntree_queries.cpp GTE/Mathematics/BallPivotMeshHoleFiller.cpp $(LDFLAGS)

# Patch cluster merger test
test_patch_cluster_merger: $(TEST_DIR)/test_patch_cluster_merger.cpp GTE/Mathematics/PatchClusterMerger.h GTE/Mathematics/Co3NeManifoldStitcher.h GTE/Mathematics/Co3Ne.h GTE/Mathematics/BallPivotReconstruction.cpp GTE/Mathematics/BallPivotMeshHoleFiller.cpp
	$(CXX) $(CXXFLAGS) -o test_patch_cluster_merger $(TEST_DIR)/test_patch_cluster_merger.cpp GTE/Mathematics/BallPivotReconstruction.cpp GTE/Mathematics/BallPivotMeshHoleFiller.cpp $(LDFLAGS)
