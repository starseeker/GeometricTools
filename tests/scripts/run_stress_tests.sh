#!/bin/bash
echo "=== STRESS TEST SUITE - All Triangulation Methods ==="
echo

for mesh in stress_*.obj; do
    basename=$(basename $mesh .obj)
    echo "================================================"
    echo "Testing: $mesh"
    echo "================================================"
    
    for method in ec cdt ec3d; do
        output="${basename}_${method}.obj"
        echo
        echo "--- Method: $method ---"
        ./test_mesh_repair $mesh $output $method 2>&1 | grep -A5 "Fill Holes"
    done
    echo
done

echo
echo "=== SUMMARY ==="
echo "Check *_ec.obj, *_cdt.obj, *_ec3d.obj for results"
