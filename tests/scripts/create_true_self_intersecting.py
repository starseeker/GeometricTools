#!/usr/bin/env python3
"""
Create a mesh with a hole that DEFINITELY self-intersects when projected
"""
import math

def write_obj(filename, vertices, triangles):
    with open(filename, 'w') as f:
        for v in vertices:
            f.write(f"v {v[0]} {v[1]} {v[2]}\n")
        for t in triangles:
            f.write(f"f {t[0]+1} {t[1]+1} {t[2]+1}\n")
    print(f"Created {filename}")

# Create a simple bow-tie shaped hole that WILL self-intersect
vertices = []
triangles = []

# Outer box
for i in range(8):
    angle = 2 * math.pi * i / 8
    r = 10.0
    vertices.append((r * math.cos(angle), r * math.sin(angle), 0))

# Hole boundary that creates a BOW-TIE when projected
# Point sequence creates crossing edges
hole_pts = []

# Create points that will cross when projected to XY plane
# We'll create 4 points that form an X pattern in 3D
# but when projected create a bow-tie

# Top-right, low Z
vertices.append((3, 3, 0))
hole_pts.append(len(vertices) - 1)

# Bottom-left, high Z
vertices.append((-3, -3, 3))
hole_pts.append(len(vertices) - 1)

# Top-left, low Z  
vertices.append((-3, 3, 0))
hole_pts.append(len(vertices) - 1)

# Bottom-right, high Z
vertices.append((3, -3, 3))
hole_pts.append(len(vertices) - 1)

# Connect outer to inner at a few points
for i in range(4):
    triangles.append((i*2, (i*2+1)%8, 8+i))

write_obj('test_bowtie.obj', vertices, triangles)

print("\nThis creates a BOW-TIE hole:")
print("  4 vertices in order create edges that cross in 2D projection")
print("  Edge (3,3,0)-(-3,-3,3) projects to (3,3)-(-3,-3)")
print("  Edge (-3,3,0)-(3,-3,3) projects to (-3,3)-(3,-3)")
print("  These edges CROSS at origin!")
print("\nThis MUST be rejected by 2D methods!")
