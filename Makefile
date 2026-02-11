# Makefile for building GTE mesh processing test programs

CXX = g++
CXXFLAGS = -std=c++17 -O2 -Wall -I. -IGTE
LDFLAGS = 

# Targets
TARGETS = test_mesh_repair test_remesh test_co3ne test_full_algorithms test_rvd demo_rvd_cvt test_remesh_comparison test_co3ne_rvd test_newton_optimizer

all: $(TARGETS)

test_mesh_repair: test_mesh_repair.cpp
	$(CXX) $(CXXFLAGS) -o test_mesh_repair test_mesh_repair.cpp $(LDFLAGS)

test_remesh: test_remesh.cpp
	$(CXX) $(CXXFLAGS) -o test_remesh test_remesh.cpp $(LDFLAGS)

test_co3ne: test_co3ne.cpp
	$(CXX) $(CXXFLAGS) -o test_co3ne test_co3ne.cpp $(LDFLAGS)

test_full_algorithms: test_full_algorithms.cpp
	$(CXX) $(CXXFLAGS) -o test_full_algorithms test_full_algorithms.cpp $(LDFLAGS)

test_rvd: test_rvd.cpp
	$(CXX) $(CXXFLAGS) -o test_rvd test_rvd.cpp $(LDFLAGS)

demo_rvd_cvt: demo_rvd_cvt.cpp
	$(CXX) $(CXXFLAGS) -o demo_rvd_cvt demo_rvd_cvt.cpp $(LDFLAGS)

test_remesh_comparison: test_remesh_comparison.cpp
	$(CXX) $(CXXFLAGS) -o test_remesh_comparison test_remesh_comparison.cpp $(LDFLAGS)

test_co3ne_rvd: test_co3ne_rvd.cpp
	$(CXX) $(CXXFLAGS) -o test_co3ne_rvd test_co3ne_rvd.cpp $(LDFLAGS)

test_newton_optimizer: test_newton_optimizer.cpp
	$(CXX) $(CXXFLAGS) -o test_newton_optimizer test_newton_optimizer.cpp $(LDFLAGS)

clean:
	rm -f $(TARGETS)

test: test_mesh_repair
	./test_mesh_repair gt.obj gt_repaired.obj

.PHONY: all clean test
