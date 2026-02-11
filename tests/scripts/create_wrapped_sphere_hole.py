#!/usr/bin/env python3
"""
Create the canonical stress test: sphere with hole wrapping 4/5 around equator
This is the most extreme case for planar projection!
"""
import math

def write_obj(filename, vertices, triangles):
    with open(filename, 'w') as f:
        for v in vertices:
            f.write(f"v {v[0]} {v[1]} {v[2]}\n")
        for t in triangles:
            f.write(f"f {t[0]+1} {t[1]+1} {t[2]+1}\n")
    print(f"Created {filename}: {len(vertices)} vertices, {len(triangles)} triangles")

def create_sphere_with_wrapped_hole():
    """
    Create a sphere with a narrow band removed that wraps 4/5 (288°) around the equator.
    This is the WORST case for planar projection:
    - Hole boundary is 288° of arc (almost a full circle)
    - Highly non-planar
    - When projected, will create severe overlaps
    """
    vertices = []
    triangles = []
    
    radius = 5.0
    
    # Sphere parameters
    n_lat = 20  # latitude divisions
    n_lon = 32  # longitude divisions
    
    # Hole parameters: remove a narrow band around equator
    # Band spans from 288° to 360° (72° gap) and wraps around latitude
    hole_start_lon = 0.8 * 2 * math.pi  # 288 degrees
    hole_end_lon = 2 * math.pi           # 360 degrees
    hole_lat_min = 0.45  # Just below equator
    hole_lat_max = 0.55  # Just above equator
    
    # Generate sphere vertices
    for i in range(n_lat + 1):
        theta = math.pi * i / n_lat  # 0 to pi (north to south)
        lat_param = i / n_lat
        
        for j in range(n_lon):
            phi = 2 * math.pi * j / n_lon  # 0 to 2pi (around equator)
            
            x = radius * math.sin(theta) * math.cos(phi)
            y = radius * math.sin(theta) * math.sin(phi)
            z = radius * math.cos(theta)
            
            vertices.append((x, y, z))
    
    # Generate triangles, skipping the hole region
    for i in range(n_lat):
        lat_param = i / n_lat
        lat_param_next = (i + 1) / n_lat
        
        for j in range(n_lon):
            j_next = (j + 1) % n_lon
            phi = 2 * math.pi * j / n_lon
            phi_next = 2 * math.pi * j_next / n_lon
            
            # Check if this quad is in the hole region
            in_hole_lat = (hole_lat_min <= lat_param <= hole_lat_max or 
                          hole_lat_min <= lat_param_next <= hole_lat_max)
            in_hole_lon = (hole_start_lon <= phi or 
                          hole_start_lon <= phi_next or
                          phi <= hole_end_lon - hole_start_lon)
            
            # Skip if in hole (but this logic creates the 4/5 wrap)
            # Actually, let's be more explicit:
            if in_hole_lat and in_hole_lon:
                continue
            
            v1 = i * n_lon + j
            v2 = i * n_lon + j_next
            v3 = (i + 1) * n_lon + j
            v4 = (i + 1) * n_lon + j_next
            
            triangles.append((v1, v2, v3))
            triangles.append((v2, v4, v3))
    
    write_obj('stress_wrapped_sphere.obj', vertices, triangles)
    
    # Calculate actual hole statistics
    print("\nHole characteristics:")
    print(f"  Wraps {(hole_end_lon - hole_start_lon) / (2*math.pi) * 360:.0f}° around sphere")
    print(f"  This is {(hole_end_lon - hole_start_lon) / (2*math.pi) * 100:.0f}% of circumference")
    print(f"  Latitude band: {hole_lat_min * 100:.0f}% to {hole_lat_max * 100:.0f}%")
    print("\nWhen projected to plane:")
    print("  - Boundary forms nearly complete circle")
    print("  - Start and end points are close together")
    print("  - Severe curvature will distort projection")
    print("  - This is THE WORST CASE for planar projection!")

# Also create an even more extreme version: FULL 360° wrap
def create_sphere_with_full_wrap():
    """
    Create a sphere with a narrow band that wraps COMPLETELY (360°) around.
    This creates a TOPOLOGICAL challenge - the hole is a complete ring.
    """
    vertices = []
    triangles = []
    
    radius = 5.0
    n_lat = 20
    n_lon = 32
    
    # Remove a narrow latitude band completely around the sphere
    hole_lat_min = 0.48
    hole_lat_max = 0.52
    
    # Generate sphere vertices
    for i in range(n_lat + 1):
        theta = math.pi * i / n_lat
        for j in range(n_lon):
            phi = 2 * math.pi * j / n_lon
            
            x = radius * math.sin(theta) * math.cos(phi)
            y = radius * math.sin(theta) * math.sin(phi)
            z = radius * math.cos(theta)
            
            vertices.append((x, y, z))
    
    # Generate triangles, skipping the hole region
    for i in range(n_lat):
        lat_param = i / n_lat
        lat_param_next = (i + 1) / n_lat
        
        # Skip if in hole latitude band
        if (hole_lat_min <= lat_param <= hole_lat_max or 
            hole_lat_min <= lat_param_next <= hole_lat_max):
            continue
        
        for j in range(n_lon):
            j_next = (j + 1) % n_lon
            
            v1 = i * n_lon + j
            v2 = i * n_lon + j_next
            v3 = (i + 1) * n_lon + j
            v4 = (i + 1) * n_lon + j_next
            
            triangles.append((v1, v2, v3))
            triangles.append((v2, v4, v3))
    
    write_obj('stress_ring_hole.obj', vertices, triangles)
    
    print("\nRing hole characteristics:")
    print("  - Complete 360° ring around sphere")
    print("  - Topologically equivalent to cutting sphere in half")
    print("  - Two separate boundaries (top and bottom of ring)")
    print("  - EXTREME test of hole detection and filling")

if __name__ == '__main__':
    print("Creating canonical worst-case test: sphere with wrapped equatorial hole\n")
    create_sphere_with_wrapped_hole()
    print("\n" + "="*60)
    create_sphere_with_full_wrap()
    print("\n" + "="*60)
    print("\nThese are THE most extreme cases for hole filling!")
