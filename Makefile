# Simple Makefile for building the mesh repair test program

CXX = g++
CXXFLAGS = -std=c++17 -O2 -Wall -I. -IGTE
LDFLAGS = 

SOURCES = test_mesh_repair.cpp
TARGET = test_mesh_repair

all: $(TARGET)

$(TARGET): $(SOURCES)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(SOURCES) $(LDFLAGS)

clean:
	rm -f $(TARGET)

test: $(TARGET)
	./$(TARGET) gt.obj gt_repaired.obj

.PHONY: all clean test
