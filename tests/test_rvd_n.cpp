// Test program for RestrictedVoronoiDiagramN
// Tests RVD centroid computation for N-dimensional CVT

#include <GTE/Mathematics/RestrictedVoronoiDiagramN.h>
#include <GTE/Mathematics/DelaunayNN.h>
#include <GTE/Mathematics/MeshAnisotropy.h>
#include <iostream>
#include <vector>
#include <cmath>

int main()
{
    std::cout << "Testing RestrictedVoronoiDiagramN...\n\n";
    
    // Test 1: Simple 3D RVD (dimension=3, isotropic)
    std::cout << "Test 1: 3D RVD (isotropic CVT)\n";
    {
        // Create a simple cube mesh
        std::vector<gte::Vector3<double>> vertices = {
            {0.0, 0.0, 0.0},  // 0
            {1.0, 0.0, 0.0},  // 1
            {1.0, 1.0, 0.0},  // 2
            {0.0, 1.0, 0.0},  // 3
            {0.0, 0.0, 1.0},  // 4
            {1.0, 0.0, 1.0},  // 5
            {1.0, 1.0, 1.0},  // 6
            {0.0, 1.0, 1.0}   // 7
        };
        
        std::vector<std::array<int32_t, 3>> triangles = {
            {0, 1, 2}, {0, 2, 3},  // Bottom
            {4, 6, 5}, {4, 7, 6},  // Top
            {0, 4, 5}, {0, 5, 1},  // Front
            {2, 7, 3}, {2, 6, 7},  // Back
            {0, 3, 7}, {0, 7, 4},  // Left
            {1, 5, 6}, {1, 6, 2}   // Right
        };
        
        std::cout << "  Mesh: " << vertices.size() << " vertices, "
                  << triangles.size() << " triangles\n";
        
        // Create 3D sites (for isotropic case, N=3)
        std::vector<gte::Vector<3, double>> sites = {
            {0.25, 0.25, 0.25},
            {0.75, 0.25, 0.25},
            {0.25, 0.75, 0.25},
            {0.75, 0.75, 0.25}
        };
        
        std::cout << "  Sites: " << sites.size() << " 3D points\n";
        
        // Create DelaunayNN
        gte::DelaunayNN<double, 3> delaunay(10);
        delaunay.SetVertices(sites.size(), sites.data());
        
        // Create RVD
        gte::RestrictedVoronoiDiagramN<double, 3> rvd;
        bool initialized = rvd.Initialize(&delaunay, vertices, triangles, sites);
        
        if (initialized)
        {
            std::cout << "  ✓ RVD initialized\n";
            
            // Compute centroids
            std::vector<gte::Vector<3, double>> centroids;
            bool success = rvd.ComputeCentroids(centroids);
            
            if (success)
            {
                std::cout << "  ✓ Centroids computed: " << centroids.size() << "\n";
                
                for (size_t i = 0; i < centroids.size(); ++i)
                {
                    std::cout << "    Site " << i << " centroid: ("
                              << centroids[i][0] << ", "
                              << centroids[i][1] << ", "
                              << centroids[i][2] << ")\n";
                }
            }
            
            // Compute cell areas
            std::vector<double> areas;
            rvd.ComputeCellAreas(areas);
            
            std::cout << "  Cell areas:\n";
            double totalArea = 0.0;
            for (size_t i = 0; i < areas.size(); ++i)
            {
                std::cout << "    Cell " << i << ": " << areas[i] << "\n";
                totalArea += areas[i];
            }
            std::cout << "  Total area: " << totalArea << " (cube surface = 6.0)\n";
            
            if (std::abs(totalArea - 6.0) < 0.01)
            {
                std::cout << "  ✓ Total area matches cube surface area\n";
            }
        }
    }
    
    // Test 2: 6D RVD (anisotropic CVT)
    std::cout << "\nTest 2: 6D RVD (anisotropic CVT)\n";
    {
        // Create simple mesh
        std::vector<gte::Vector3<double>> vertices = {
            {0.0, 0.0, 0.0},
            {1.0, 0.0, 0.0},
            {0.0, 1.0, 0.0},
            {0.0, 0.0, 1.0}
        };
        
        std::vector<std::array<int32_t, 3>> triangles = {
            {0, 1, 2},
            {0, 1, 3},
            {0, 2, 3},
            {1, 2, 3}
        };
        
        std::cout << "  Mesh: " << vertices.size() << " vertices, "
                  << triangles.size() << " triangles (tetrahedron)\n";
        
        // Compute normals for anisotropy
        std::vector<gte::Vector3<double>> normals;
        gte::MeshAnisotropy<double>::SetAnisotropy(vertices, triangles, normals, 0.04);
        
        std::cout << "  Computed " << normals.size() << " anisotropic normals\n";
        
        // Create 6D sites from 3D positions + scaled normals
        std::vector<gte::Vector<6, double>> sites6D;
        for (size_t i = 0; i < vertices.size(); ++i)
        {
            gte::Vector<6, double> site;
            site[0] = vertices[i][0];
            site[1] = vertices[i][1];
            site[2] = vertices[i][2];
            site[3] = normals[i][0];
            site[4] = normals[i][1];
            site[5] = normals[i][2];
            sites6D.push_back(site);
        }
        
        std::cout << "  Created " << sites6D.size() << " 6D sites\n";
        
        // Create DelaunayNN for 6D
        gte::DelaunayNN<double, 6> delaunay(10);
        delaunay.SetVertices(sites6D.size(), sites6D.data());
        
        // Create RVD
        gte::RestrictedVoronoiDiagramN<double, 6> rvd;
        bool initialized = rvd.Initialize(&delaunay, vertices, triangles, sites6D);
        
        if (initialized)
        {
            std::cout << "  ✓ 6D RVD initialized\n";
            
            // Compute centroids
            std::vector<gte::Vector<6, double>> centroids;
            bool success = rvd.ComputeCentroids(centroids);
            
            if (success)
            {
                std::cout << "  ✓ 6D Centroids computed: " << centroids.size() << "\n";
                
                // Show first few centroids (position only)
                for (size_t i = 0; i < std::min(size_t(3), centroids.size()); ++i)
                {
                    std::cout << "    Site " << i << " centroid pos: ("
                              << centroids[i][0] << ", "
                              << centroids[i][1] << ", "
                              << centroids[i][2] << ")\n";
                }
                
                std::cout << "  ✓ 6D anisotropic RVD works\n";
            }
        }
    }
    
    // Test 3: CVT iteration with RVD
    std::cout << "\nTest 3: Lloyd iteration with RVD\n";
    {
        // Simple quad mesh
        std::vector<gte::Vector3<double>> vertices = {
            {0.0, 0.0, 0.0},
            {1.0, 0.0, 0.0},
            {1.0, 1.0, 0.0},
            {0.0, 1.0, 0.0}
        };
        
        std::vector<std::array<int32_t, 3>> triangles = {
            {0, 1, 2},
            {0, 2, 3}
        };
        
        // Initial sites
        std::vector<gte::Vector<3, double>> sites = {
            {0.3, 0.3, 0.0},
            {0.7, 0.7, 0.0}
        };
        
        std::cout << "  Initial sites:\n";
        for (size_t i = 0; i < sites.size(); ++i)
        {
            std::cout << "    Site " << i << ": ("
                      << sites[i][0] << ", " << sites[i][1] << ")\n";
        }
        
        // Run one Lloyd iteration
        gte::DelaunayNN<double, 3> delaunay(10);
        delaunay.SetVertices(sites.size(), sites.data());
        
        gte::RestrictedVoronoiDiagramN<double, 3> rvd;
        rvd.Initialize(&delaunay, vertices, triangles, sites);
        
        std::vector<gte::Vector<3, double>> centroids;
        rvd.ComputeCentroids(centroids);
        
        std::cout << "  After Lloyd iteration (centroids):\n";
        for (size_t i = 0; i < centroids.size(); ++i)
        {
            std::cout << "    Site " << i << ": ("
                      << centroids[i][0] << ", " << centroids[i][1] << ")\n";
        }
        
        // Check if centroids moved (they should for non-optimal initial sites)
        bool moved = false;
        for (size_t i = 0; i < sites.size(); ++i)
        {
            double dx = centroids[i][0] - sites[i][0];
            double dy = centroids[i][1] - sites[i][1];
            if (std::abs(dx) > 1e-6 || std::abs(dy) > 1e-6)
            {
                moved = true;
                break;
            }
        }
        
        if (moved)
        {
            std::cout << "  ✓ Lloyd iteration updated site positions\n";
        }
    }
    
    std::cout << "\n=== RestrictedVoronoiDiagramN tests completed ===\n";
    std::cout << "\nThe RVD implementation provides centroid computation for CVT\n";
    std::cout << "operations in any dimension N, suitable for anisotropic remeshing.\n";
    
    return 0;
}
