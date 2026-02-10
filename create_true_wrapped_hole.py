#!/usr/bin/env python3
"""
Create sphere with hole wrapping 4/5 (288°) around equator
"""
import math

def write_obj(filename, vertices, triangles):
    with open(filename, 'w') as f:
        for v in vertices:
            f.write(f"v {v[0]} {v[1]} {v[2]}\n")
        for t in triangles:
            f.write(f"f {t[0]+1} {t[1]+1} {t[2]+1}\n")
    print(f"Created {filename}: {len(vertices)} vertices, {len(triangles)} triangles")

vertices = []
triangles = []

radius = 5.0
n_lat = 20
n_lon = 40  # Finer longitude divisions

# Generate sphere
for i in range(n_lat + 1):
    theta = math.pi * i / n_lat  # 0 to pi
    for j in range(n_lon):
        phi = 2 * math.pi * j / n_lon  # 0 to 2pi
        
        x = radius * math.sin(theta) * math.cos(phi)
        y = radius * math.sin(theta) * math.sin(phi)
        z = radius * math.cos(theta)
        
        vertices.append((x, y, z))

# Hole region: narrow latitude band, wide longitude span
# Longitude: 0° to 288° (KEEP this region as a hole)
# Small gap: 288° to 360° (KEEP these triangles)
hole_lon_start = 0.0  # Start of hole
hole_lon_end = 0.8 * 2 * math.pi  # End of hole (288°)
gap_start = hole_lon_end
gap_end = 2 * math.pi

# Latitude: narrow band around equator
hole_lat_min = 0.47  # Just below equator  
hole_lat_max = 0.53  # Just above equator

# Generate triangles
for i in range(n_lat):
    lat_param = i / n_lat
    lat_next = (i + 1) / n_lat
    
    for j in range(n_lon):
        j_next = (j + 1) % n_lon
        lon_param = 2 * math.pi * j / n_lon
        lon_next = 2 * math.pi * j_next / n_lon
        
        # Check if in hole latitude band
        in_hole_lat = (hole_lat_min <= lat_param <= hole_lat_max)
        
        # Check if in hole longitude range (0° to 288°)
        # Keep only if in the small gap (288° to 360°)
        in_gap = (gap_start <= lon_param < gap_end or 
                  gap_start <= lon_next < gap_end)
        
        # Skip if in hole region
        if in_hole_lat and not in_gap:
            continue
        
        v1 = i * n_lon + j
        v2 = i * n_lon + j_next
        v3 = (i + 1) * n_lon + j
        v4 = (i + 1) * n_lon + j_next
        
        triangles.append((v1, v2, v3))
        triangles.append((v2, v4, v3))

write_obj('stress_wrapped_4_5.obj', vertices, triangles)

print("\n=== CANONICAL WORST CASE ===")
print(f"Hole wraps {(hole_lon_end - hole_lon_start) / (2*math.pi) * 360:.0f}° around equator")
print(f"This is {(hole_lon_end - hole_lon_start) / (2*math.pi):.1%} of circumference")
print(f"Small gap: {(gap_end - gap_start) / (2*math.pi) * 360:.0f}°")
print(f"Latitude band width: {(hole_lat_max - hole_lat_min) * 100:.0f}% of sphere")
print("\nThis creates a NEARLY-COMPLETE ring around the sphere!")
print("When projected to average plane:")
print("  - Boundary wraps 288° (4/5 of full circle)")
print("  - Will create severe self-overlap")
print("  - Highly non-planar (spans entire longitude)")
print("  - This is THE definitive stress test!")
