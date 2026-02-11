#!/usr/bin/env python3
"""
Create stress test OBJ meshes with different types of holes
"""
import math

def write_obj(filename, vertices, triangles):
    with open(filename, 'w') as f:
        for v in vertices:
            f.write(f"v {v[0]} {v[1]} {v[2]}\n")
        for t in triangles:
            f.write(f"f {t[0]+1} {t[1]+1} {t[2]+1}\n")  # OBJ is 1-indexed
    print(f"Created {filename}: {len(vertices)} vertices, {len(triangles)} triangles")

# Test 1: Non-planar hole (spherical cap with hole at top)
def create_nonplanar_hole_mesh():
    vertices = []
    triangles = []
    
    # Create sphere vertices
    n_lat = 12
    n_lon = 16
    radius = 1.0
    
    for i in range(n_lat + 1):
        theta = math.pi * i / n_lat  # 0 to pi
        for j in range(n_lon):
            phi = 2 * math.pi * j / n_lon
            x = radius * math.sin(theta) * math.cos(phi)
            y = radius * math.sin(theta) * math.sin(phi)
            z = radius * math.cos(theta)
            vertices.append((x, y, z))
    
    # Create triangles (skip top rows to create hole)
    hole_rows = 3  # Skip top 3 rows
    for i in range(hole_rows, n_lat):
        for j in range(n_lon):
            j_next = (j + 1) % n_lon
            
            v1 = i * n_lon + j
            v2 = i * n_lon + j_next
            v3 = (i + 1) * n_lon + j
            v4 = (i + 1) * n_lon + j_next
            
            triangles.append((v1, v2, v3))
            triangles.append((v2, v4, v3))
    
    write_obj('stress_nonplanar.obj', vertices, triangles)

# Test 2: Nearly degenerate hole (thin strip with small gap)
def create_degenerate_hole_mesh():
    vertices = []
    triangles = []
    
    # Create long thin mesh with gap in middle
    n = 20
    width = 0.1
    length = 10.0
    gap_start = 8
    gap_end = 12
    
    for i in range(n + 1):
        t = i / n
        x = t * length
        vertices.append((x, 0, 0))
        vertices.append((x, width, 0))
    
    # Create triangles (skip gap region)
    for i in range(n):
        if gap_start <= i < gap_end:
            continue  # Create hole
        
        v1 = i * 2
        v2 = i * 2 + 1
        v3 = (i + 1) * 2
        v4 = (i + 1) * 2 + 1
        
        triangles.append((v1, v2, v3))
        triangles.append((v2, v4, v3))
    
    write_obj('stress_degenerate.obj', vertices, triangles)

# Test 3: Elongated hole (very long, thin rectangle missing)
def create_elongated_hole_mesh():
    vertices = []
    triangles = []
    
    # Create rectangle mesh with long thin hole
    nx = 50
    ny = 5
    lx = 100.0
    ly = 5.0
    
    for j in range(ny + 1):
        for i in range(nx + 1):
            x = i * lx / nx
            y = j * ly / ny
            vertices.append((x, y, 0))
    
    # Create triangles (skip middle row to create elongated hole)
    hole_y = 2  # Middle row
    for j in range(ny):
        if j == hole_y:
            continue
        for i in range(nx):
            v1 = j * (nx + 1) + i
            v2 = v1 + 1
            v3 = (j + 1) * (nx + 1) + i
            v4 = v3 + 1
            
            triangles.append((v1, v2, v3))
            triangles.append((v2, v4, v3))
    
    write_obj('stress_elongated.obj', vertices, triangles)

# Test 4: Concave hole (star-shaped gap)
def create_concave_hole_mesh():
    vertices = []
    triangles = []
    
    # Create circle with star-shaped hole in center
    outer_r = 10.0
    inner_r_out = 4.0
    inner_r_in = 2.0
    n_pts = 16
    
    # Outer circle
    outer_indices = []
    for i in range(n_pts):
        angle = 2 * math.pi * i / n_pts
        x = outer_r * math.cos(angle)
        y = outer_r * math.sin(angle)
        vertices.append((x, y, 0))
        outer_indices.append(len(vertices) - 1)
    
    # Star-shaped inner boundary
    star_indices = []
    for i in range(n_pts * 2):
        angle = math.pi * i / n_pts
        r = inner_r_out if i % 2 == 0 else inner_r_in
        x = r * math.cos(angle)
        y = r * math.sin(angle)
        vertices.append((x, y, 0))
        star_indices.append(len(vertices) - 1)
    
    # Create triangles connecting outer to inner (but not filling center)
    # This creates a star-shaped hole
    for i in range(n_pts):
        i_next = (i + 1) % n_pts
        
        # Connect to star points (every other one)
        star_i = i * 2
        star_i_next = ((i + 1) * 2) % len(star_indices)
        
        outer_v1 = outer_indices[i]
        outer_v2 = outer_indices[i_next]
        star_v1 = star_indices[star_i]
        star_v2 = star_indices[star_i_next]
        
        triangles.append((outer_v1, outer_v2, star_v1))
        triangles.append((outer_v2, star_v2, star_v1))
    
    write_obj('stress_concave.obj', vertices, triangles)

# Test 5: Large complex hole
def create_large_hole_mesh():
    vertices = []
    triangles = []
    
    # Create grid with large irregular hole
    nx = 30
    ny = 30
    size = 20.0
    
    for j in range(ny + 1):
        for i in range(nx + 1):
            x = i * size / nx - size / 2
            y = j * size / ny - size / 2
            vertices.append((x, y, 0))
    
    # Create triangles except in center region with wavy boundary
    for j in range(ny):
        for i in range(nx):
            # Skip if inside wavy hole boundary
            x = (i + 0.5) * size / nx - size / 2
            y = (j + 0.5) * size / ny - size / 2
            
            angle = math.atan2(y, x)
            r = math.sqrt(x*x + y*y)
            hole_r = 5.0 + 1.5 * math.sin(5 * angle)  # Wavy boundary
            
            if r < hole_r:
                continue  # Inside hole
            
            v1 = j * (nx + 1) + i
            v2 = v1 + 1
            v3 = (j + 1) * (nx + 1) + i
            v4 = v3 + 1
            
            triangles.append((v1, v2, v3))
            triangles.append((v2, v4, v3))
    
    write_obj('stress_large.obj', vertices, triangles)

if __name__ == '__main__':
    print("Creating stress test meshes...")
    create_nonplanar_hole_mesh()
    create_degenerate_hole_mesh()
    create_elongated_hole_mesh()
    create_concave_hole_mesh()
    create_large_hole_mesh()
    print("\nAll stress test meshes created!")
