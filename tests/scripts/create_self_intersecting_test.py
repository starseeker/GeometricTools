#!/usr/bin/env python3
"""
Create a mesh with a hole that will self-intersect when projected to 2D
"""
import math

def write_obj(filename, vertices, triangles):
    with open(filename, 'w') as f:
        for v in vertices:
            f.write(f"v {v[0]} {v[1]} {v[2]}\n")
        for t in triangles:
            f.write(f"f {t[0]+1} {t[1]+1} {t[2]+1}\n")
    print(f"Created {filename}: {len(vertices)} vertices, {len(triangles)} triangles")

def create_twisted_boundary_mesh():
    """
    Create a mesh with a twisted/spiral hole boundary.
    When projected to the average plane, edges will cross.
    """
    vertices = []
    triangles = []
    
    # Create a twisted rectangular boundary
    # The hole boundary spirals in 3D, causing self-intersection when flattened
    n_steps = 20
    width = 10.0
    height = 5.0
    twist_amount = math.pi  # 180 degree twist
    
    # Outer boundary (no twist)
    outer_pts = []
    for i in range(4):
        if i == 0:
            x, y = -width/2, -height/2
        elif i == 1:
            x, y = width/2, -height/2
        elif i == 2:
            x, y = width/2, height/2
        else:
            x, y = -width/2, height/2
        vertices.append((x, y, 0))
        outer_pts.append(len(vertices) - 1)
    
    # Inner boundary (twisted) - This will self-intersect when projected!
    # Create a rectangle that rotates as it moves in Z
    inner_pts = []
    inner_width = 4.0
    inner_height = 2.0
    
    for i in range(n_steps):
        t = i / (n_steps - 1)
        angle = t * twist_amount
        z = t * 2.0 - 1.0  # Vary Z from -1 to 1
        
        # Rotate rectangle corners around Z axis
        corners = [
            (-inner_width/2, -inner_height/2),
            (inner_width/2, -inner_height/2),
            (inner_width/2, inner_height/2),
            (-inner_width/2, inner_height/2),
        ]
        
        # Only add one point per step to create a twisted loop
        corner_idx = i % 4
        x0, y0 = corners[corner_idx]
        
        # Rotate by angle
        x = x0 * math.cos(angle) - y0 * math.sin(angle)
        y = x0 * math.sin(angle) + y0 * math.cos(angle)
        
        vertices.append((x, y, z))
        inner_pts.append(len(vertices) - 1)
    
    # Create triangles connecting outer to inner (everywhere except the hole)
    # Connect outer rectangle to start of inner twisted boundary
    for i in range(4):
        i_next = (i + 1) % 4
        inner_i = i * (n_steps // 4)
        inner_i_next = min(inner_i + (n_steps // 4), n_steps - 1)
        
        triangles.append((outer_pts[i], outer_pts[i_next], inner_pts[inner_i]))
        if inner_i_next < len(inner_pts):
            triangles.append((outer_pts[i_next], inner_pts[inner_i_next], inner_pts[inner_i]))
    
    write_obj('stress_self_intersecting.obj', vertices, triangles)
    
    # Also create a simpler version: figure-8 shaped hole
    vertices2 = []
    triangles2 = []
    
    # Outer square
    size = 10.0
    for i in range(4):
        x = size/2 if i % 2 == 1 else -size/2
        y = size/2 if i >= 2 else -size/2
        vertices2.append((x, y, 0))
    
    # Figure-8 boundary (crosses itself in 2D projection)
    # Create a lemniscate (figure-8) curve that rises out of plane
    fig8_pts = []
    n = 30
    a = 3.0  # Size parameter
    
    for i in range(n):
        t = 2 * math.pi * i / n
        # Lemniscate of Bernoulli in XY
        x = a * math.cos(t) / (1 + math.sin(t)**2)
        y = a * math.sin(t) * math.cos(t) / (1 + math.sin(t)**2)
        # Add Z variation to make it 3D
        z = 0.5 * math.sin(2 * t)  # Rises out of plane
        
        vertices2.append((x, y, z))
        fig8_pts.append(len(vertices2) - 1)
    
    # Connect outer to inner at a few points
    for i in range(4):
        i_next = (i + 1) % 4
        fig_i = i * (n // 4)
        fig_i_next = ((i + 1) * (n // 4)) % n
        
        triangles2.append((i, i_next, fig8_pts[fig_i]))
        triangles2.append((i_next, fig8_pts[fig_i_next], fig8_pts[fig_i]))
    
    write_obj('stress_figure8.obj', vertices2, triangles2)

def create_saddle_hole_mesh():
    """
    Create a saddle-shaped surface with a hole.
    The hole boundary is non-planar in a way that creates
    overlapping regions when projected.
    """
    vertices = []
    triangles = []
    
    # Create a grid for the saddle surface
    n = 20
    size = 10.0
    
    for j in range(n + 1):
        for i in range(n + 1):
            x = (i / n - 0.5) * size
            y = (j / n - 0.5) * size
            
            # Saddle shape: z = x^2 - y^2
            z = 0.3 * (x * x - y * y)
            
            vertices.append((x, y, z))
    
    # Create triangles, skipping center region to create hole
    hole_radius = 3.0
    for j in range(n):
        for i in range(n):
            x = ((i + 0.5) / n - 0.5) * size
            y = ((j + 0.5) / n - 0.5) * size
            
            # Skip if in hole region (but with irregular boundary)
            if x*x + y*y < hole_radius * hole_radius:
                continue
            
            v1 = j * (n + 1) + i
            v2 = v1 + 1
            v3 = (j + 1) * (n + 1) + i
            v4 = v3 + 1
            
            triangles.append((v1, v2, v3))
            triangles.append((v2, v4, v3))
    
    write_obj('stress_saddle.obj', vertices, triangles)

if __name__ == '__main__':
    print("Creating self-intersecting projection test meshes...")
    create_twisted_boundary_mesh()
    create_saddle_hole_mesh()
    print("\nTest meshes created!")
    print("\nThese meshes have holes that may self-intersect when projected to 2D:")
    print("  - stress_self_intersecting.obj: Twisted spiral boundary")
    print("  - stress_figure8.obj: Figure-8 (lemniscate) boundary") 
    print("  - stress_saddle.obj: Hole on saddle surface")
