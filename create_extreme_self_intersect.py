#!/usr/bin/env python3
"""
Create an EXTREME case that MUST self-intersect when projected
"""
import math

def write_obj(filename, vertices, triangles):
    with open(filename, 'w') as f:
        for v in vertices:
            f.write(f"v {v[0]} {v[1]} {v[2]}\n")
        for t in triangles:
            f.write(f"f {t[0]+1} {t[1]+1} {t[2]+1}\n")
    print(f"Created {filename}")

# Create a box with a hole whose boundary explicitly crosses itself in projection
vertices = []
triangles = []

# Outer box (large, flat)
box_size = 20.0
for i in range(4):
    x = box_size/2 if i % 2 == 1 else -box_size/2
    y = box_size/2 if i >= 2 else -box_size/2
    vertices.append((x, y, 0))

# Create a hole boundary that EXPLICITLY crosses itself
# Imagine looking down from above (Z axis):
# The boundary goes: (2,0,0) -> (-2,0,1) -> (-2,0,0) -> (2,0,1) 
# This creates an X pattern when projected to XY plane

hole_boundary = []

# Point 1: Right side, bottom
vertices.append((3, 0, 0))
hole_boundary.append(len(vertices) - 1)

# Point 2: Left side, TOP (different Z creates crossing when projected)
vertices.append((-3, 0, 2))  # High Z
hole_boundary.append(len(vertices) - 1)

# Point 3: Top side, bottom
vertices.append((0, 3, 0))
hole_boundary.append(len(vertices) - 1)

# Point 4: Bottom side, TOP (crosses the first edge)  
vertices.append((0, -3, 2))  # High Z
hole_boundary.append(len(vertices) - 1)

# Connect outer box to hole boundary
for i in range(4):
    i_next = (i + 1) % 4
    h_i = i
    triangles.append((i, i_next, 4 + h_i))

write_obj('stress_explicit_crossing.obj', vertices, triangles)

print("\nThis mesh has a 4-vertex hole boundary that EXPLICITLY crosses itself:")
print("  When projected to XY plane:")
print("    Edge (3,0,0)-(-3,0,2) projects to (3,0)-(-3,0)")
print("    Edge (0,3,0)-(0,-3,2) projects to (0,3)-(0,-3)")
print("  These two edges cross at the origin!")

# Also create a helix-shaped hole
vertices2 = []
triangles2 = []

# Outer boundary
for i in range(8):
    angle = 2 * math.pi * i / 8
    r = 10.0
    vertices2.append((r * math.cos(angle), r * math.sin(angle), 0))

# Helix hole boundary (rises as it spirals)
helix_pts = []
n = 16
radius = 3.0
for i in range(n):
    angle = 4 * math.pi * i / n  # Two full rotations
    z = 5.0 * i / n  # Rises
    x = radius * math.cos(angle)
    y = radius * math.sin(angle)
    vertices2.append((x, y, z))
    helix_pts.append(len(vertices2) - 1)

# Connect outer to helix start
for i in range(8):
    i_next = (i + 1) % 8
    h_i = i * 2
    h_i_next = (i + 1) * 2
    triangles2.append((i, i_next, 8 + h_i))

write_obj('stress_helix.obj', vertices2, triangles2)
print("\nCreated stress_helix.obj: Helix-shaped hole that spirals in Z")
