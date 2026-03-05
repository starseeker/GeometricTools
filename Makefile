# Makefile for building GTE mesh processing test programs

CXX = g++
CXXFLAGS = -std=c++17 -O2 -Wall -I. -IGTE
LDFLAGS = 
PTHREAD = -pthread

# Test source directory
TEST_DIR = tests

# Geogram submodule paths (used by test_geogram_comparison)
GEOGRAM_SRC = geogram/src/lib
GEOGRAM_INC = -I$(GEOGRAM_SRC) -I$(GEOGRAM_SRC)/geogram/third_party/OpenNL
GEOGRAM_BUILD_DIR = /tmp/geogram_build
GEOGRAM_LIB_DIR = $(GEOGRAM_BUILD_DIR)/lib
GEOGRAM_LIBS = -L$(GEOGRAM_LIB_DIR) -lgeogram -Wl,-rpath,$(GEOGRAM_LIB_DIR)

# Primary test targets
TARGETS = test_mesh_repair test_full_algorithms test_rvd \
          demo_rvd_cvt test_remesh_comparison test_newton_optimizer \
          test_rvd_performance stress_test test_threadpool test_parallel_rvd \
          test_anisotropic_remesh test_delaunay6 test_cvt6d \
          test_delaunay_n test_delaunay_nn test_rvd_n test_cvt_n \
          test_anisotropic_end_to_end \
          compare_with_geogram

all: $(TARGETS)

# Basic functionality tests
test_mesh_repair: $(TEST_DIR)/test_mesh_repair.cpp \
                  GTE/Mathematics/MeshHoleFilling.h \
                  GTE/Mathematics/LSCMParameterization.h
	$(CXX) $(CXXFLAGS) -o test_mesh_repair $(TEST_DIR)/test_mesh_repair.cpp $(LDFLAGS)

test_remesh: $(TEST_DIR)/test_remesh.cpp
	$(CXX) $(CXXFLAGS) -o test_remesh $(TEST_DIR)/test_remesh.cpp $(LDFLAGS)

test_full_algorithms: $(TEST_DIR)/test_full_algorithms.cpp
	$(CXX) $(CXXFLAGS) -o test_full_algorithms $(TEST_DIR)/test_full_algorithms.cpp $(LDFLAGS)

test_rvd: $(TEST_DIR)/test_rvd.cpp
	$(CXX) $(CXXFLAGS) -o test_rvd $(TEST_DIR)/test_rvd.cpp $(LDFLAGS)

# Comparison and validation
test_remesh_comparison: $(TEST_DIR)/test_remesh_comparison.cpp
	$(CXX) $(CXXFLAGS) -o test_remesh_comparison $(TEST_DIR)/test_remesh_comparison.cpp $(LDFLAGS)

# GTE-only comparison (no geogram required, compares GTE output with a pre-generated geogram .obj)
compare_with_geogram: $(TEST_DIR)/compare_with_geogram.cpp
	$(CXX) $(CXXFLAGS) -o compare_with_geogram $(TEST_DIR)/compare_with_geogram.cpp $(LDFLAGS)

# Build the geogram library from the submodule.
# Initializes the required nested submodules (OpenNL, libMeshb, rply, amgcl) and
# runs a Release cmake build into $(GEOGRAM_BUILD_DIR).
build_geogram:
	@echo "Initializing geogram submodule and its dependencies..."
	git submodule update --init geogram
	cd geogram && git submodule update --init \
	    src/lib/geogram/third_party/OpenNL \
	    src/lib/geogram/third_party/libMeshb \
	    src/lib/geogram/third_party/rply \
	    src/lib/geogram/third_party/amgcl
	@echo "Configuring geogram (Release, no graphics)..."
	mkdir -p $(GEOGRAM_BUILD_DIR)
	cd $(GEOGRAM_BUILD_DIR) && cmake $(CURDIR)/geogram \
	    -DGEOGRAM_WITH_GRAPHICS=OFF \
	    -DGEOGRAM_WITH_TETGEN=OFF \
	    -DGEOGRAM_WITH_TRIANGLE=OFF \
	    -DGEOGRAM_WITH_LEGACY_NUMERICS=OFF \
	    -DCMAKE_BUILD_TYPE=Release
	@echo "Building libgeogram..."
	$(MAKE) -C $(GEOGRAM_BUILD_DIR) -j$$(nproc) geogram
	@echo "geogram built successfully: $(GEOGRAM_LIB_DIR)/libgeogram.so"

# Direct GTE vs Geogram comparison (requires geogram submodule to be built).
# Run 'make build_geogram' first, or set GEOGRAM_LIB_DIR to an existing build.
test_geogram_comparison: $(TEST_DIR)/test_geogram_comparison.cpp \
                         GTE/Mathematics/MeshRepair.h \
                         GTE/Mathematics/MeshHoleFilling.h \
                         GTE/Mathematics/MeshValidation.h \
                         GTE/Mathematics/MeshRemesh.h \
                         GTE/Mathematics/MeshAnisotropy.h \
                         GTE/Mathematics/SurfaceRVDN.h \
                         GTE/Mathematics/CVTN.h \
                         GTE/Mathematics/DelaunayNN.h
	$(CXX) $(CXXFLAGS) $(GEOGRAM_INC) -o test_geogram_comparison \
	    $(TEST_DIR)/test_geogram_comparison.cpp \
	    $(GEOGRAM_LIBS) $(LDFLAGS)

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
test_anisotropic_remesh: $(TEST_DIR)/test_anisotropic_remesh.cpp GTE/Mathematics/MeshAnisotropy.h GTE/Mathematics/MeshRemesh.h GTE/Mathematics/SurfaceRVDN.h
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
test_cvt_n: $(TEST_DIR)/test_cvt_n.cpp GTE/Mathematics/CVTN.h GTE/Mathematics/RestrictedVoronoiDiagramN.h GTE/Mathematics/DelaunayNN.h GTE/Mathematics/MeshAnisotropy.h GTE/Mathematics/SurfaceRVDN.h
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

# Run GTE vs Geogram comparison (requires test_geogram_comparison to be built first)
# Runs both use cases: repair+hole-filling and anisotropic remeshing
# To build geogram automatically: make build_geogram test_geogram_comparison
test_geogram: test_geogram_comparison
	@echo "Running GTE vs Geogram comparison (repair, remesh)..."
	./test_geogram_comparison $(TEST_DIR)/data/gt.obj

stress: stress_test
	@echo "Running comprehensive stress tests..."
	./stress_test

# Clean up
clean:
	rm -f $(TARGETS) test_geogram_comparison
	rm -f $(TEST_DIR)/data/*_repaired.obj $(TEST_DIR)/data/*_output.obj

.PHONY: all clean test test_geogram stress generate_test_data build_geogram

# Phase 4 integration test
test_phase4_integration: $(TEST_DIR)/test_phase4_integration.cpp GTE/Mathematics/CVTN.h GTE/Mathematics/MeshRemesh.h GTE/Mathematics/SurfaceRVDN.h
	$(CXX) $(CXXFLAGS) -o test_phase4_integration $(TEST_DIR)/test_phase4_integration.cpp $(LDFLAGS)

# Comprehensive anisotropic remeshing end-to-end test
test_anisotropic_end_to_end: $(TEST_DIR)/test_anisotropic_end_to_end.cpp GTE/Mathematics/MeshRemesh.h GTE/Mathematics/CVTN.h GTE/Mathematics/MeshAnisotropy.h GTE/Mathematics/SurfaceRVDN.h
	$(CXX) $(CXXFLAGS) -o test_anisotropic_end_to_end $(TEST_DIR)/test_anisotropic_end_to_end.cpp $(LDFLAGS)

# BoundaryPolygonRTree correctness and performance test
test_boundary_polygon_rtree: $(TEST_DIR)/test_boundary_polygon_rtree.cpp GTE/Mathematics/BoundaryPolygonRTree.h
	$(CXX) $(CXXFLAGS) -o test_boundary_polygon_rtree $(TEST_DIR)/test_boundary_polygon_rtree.cpp $(LDFLAGS)
