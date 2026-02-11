# Makefile for building GTE mesh processing test programs

CXX = g++
CXXFLAGS = -std=c++17 -O2 -Wall -I. -IGTE
LDFLAGS = 
PTHREAD = -pthread

# Test source directory
TEST_DIR = tests

# Primary test targets
TARGETS = test_mesh_repair test_remesh test_co3ne test_full_algorithms test_rvd \
          demo_rvd_cvt test_remesh_comparison test_co3ne_rvd test_newton_optimizer \
          test_rvd_performance stress_test test_threadpool test_parallel_rvd \
          test_enhanced_manifold test_anisotropic_remesh

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
test_anisotropic_remesh: $(TEST_DIR)/test_anisotropic_remesh.cpp GTE/Mathematics/MeshAnisotropy.h GTE/Mathematics/MeshRemeshFull.h
	$(CXX) $(CXXFLAGS) -o test_anisotropic_remesh $(TEST_DIR)/test_anisotropic_remesh.cpp $(LDFLAGS)

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
