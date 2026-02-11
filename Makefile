# Makefile for building GTE mesh processing test programs

CXX = g++
CXXFLAGS = -std=c++17 -O2 -Wall -I. -IGTE
LDFLAGS = 

# Targets
TARGETS = test_mesh_repair test_remesh test_co3ne test_full_algorithms test_rvd

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

clean:
	rm -f $(TARGETS)

test: test_mesh_repair
	./test_mesh_repair gt.obj gt_repaired.obj

.PHONY: all clean test
