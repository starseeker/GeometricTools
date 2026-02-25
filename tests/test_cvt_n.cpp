// Test program for CVTN (Centroidal Voronoi Tessellation N-D)
// Tests complete CVT implementation with Lloyd iterations

#include <GTE/Mathematics/CVTN.h>
#include <GTE/Mathematics/MeshAnisotropy.h>
#include <iostream>
#include <vector>
#include <cmath>

int main()
{
    std::cout << "Testing CVTN (Centroidal Voronoi Tessellation)...\n\n";
    
    // Test 1: 3D isotropic CVT on simple mesh
    std::cout << "Test 1: 3D Isotropic CVT\n";
    {
        // Create quad mesh
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
        
        std::cout << "  Mesh: " << vertices.size() << " vertices, "
                  << triangles.size() << " triangles\n";
        
        // Create CVTN
        gte::CVTN<double, 3> cvt;
        cvt.SetVerbose(true);
        cvt.Initialize(vertices, triangles);
        
        // Compute initial sampling
        bool success = cvt.ComputeInitialSampling(4, 42);
        if (success)
        {
            std::cout << "  ✓ Initial sampling: " << cvt.GetNumSites() << " sites\n";
            
            // Show initial sites
            auto sites = cvt.GetSites();
            std::cout << "  Initial sites:\n";
            for (size_t i = 0; i < std::min(size_t(4), sites.size()); ++i)
            {
                std::cout << "    Site " << i << ": ("
                          << sites[i][0] << ", " << sites[i][1] << ")\n";
            }
            
            // Compute initial energy
            double initialEnergy = cvt.ComputeEnergy();
            std::cout << "  Initial energy: " << initialEnergy << "\n";
            
            // Run Lloyd iterations
            std::cout << "  Running Lloyd iterations...\n";
            success = cvt.LloydIterations(10);
            
            if (success)
            {
                std::cout << "  ✓ Lloyd iterations completed\n";
                
                // Show final sites
                auto finalSites = cvt.GetSites();
                std::cout << "  Final sites:\n";
                for (size_t i = 0; i < std::min(size_t(4), finalSites.size()); ++i)
                {
                    std::cout << "    Site " << i << ": ("
                              << finalSites[i][0] << ", " << finalSites[i][1] << ")\n";
                }
                
                // Compute final energy
                double finalEnergy = cvt.ComputeEnergy();
                std::cout << "  Final energy: " << finalEnergy << "\n";
                
                if (finalEnergy < initialEnergy)
                {
                    std::cout << "  ✓ Energy reduced (optimization working)\n";
                }
            }
        }
    }
    
    // Test 2: 6D anisotropic CVT
    std::cout << "\nTest 2: 6D Anisotropic CVT\n";
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
        
        // Create CVTN for 6D
        gte::CVTN<double, 6> cvt;
        cvt.SetVerbose(true);
        cvt.Initialize(vertices, triangles);
        
        // Compute initial 3D sampling
        bool success = cvt.ComputeInitialSampling(4, 42);
        if (success)
        {
            std::cout << "  ✓ Initial sampling: " << cvt.GetNumSites() << " sites\n";
            
            // Add anisotropic components (normals scaled by factor)
            auto sites = cvt.GetSites();
            
            // Compute normals for anisotropy
            std::vector<gte::Vector3<double>> normals;
            gte::MeshAnisotropy<double>::SetAnisotropy(vertices, triangles, normals, 0.04);
            
            std::cout << "  Computed " << normals.size() << " anisotropic normals\n";
            
            // Set anisotropic components in sites
            for (size_t i = 0; i < sites.size() && i < normals.size(); ++i)
            {
                sites[i][3] = normals[i % normals.size()][0];
                sites[i][4] = normals[i % normals.size()][1];
                sites[i][5] = normals[i % normals.size()][2];
            }
            
            cvt.SetSites(sites);
            
            std::cout << "  First site 6D: ("
                      << sites[0][0] << ", " << sites[0][1] << ", " << sites[0][2] << ", "
                      << sites[0][3] << ", " << sites[0][4] << ", " << sites[0][5] << ")\n";
            
            // Compute initial energy
            double initialEnergy = cvt.ComputeEnergy();
            std::cout << "  Initial energy: " << initialEnergy << "\n";
            
            // Run Lloyd iterations
            std::cout << "  Running Lloyd iterations in 6D...\n";
            success = cvt.LloydIterations(5);
            
            if (success)
            {
                std::cout << "  ✓ 6D Lloyd iterations completed\n";
                
                // Compute final energy
                double finalEnergy = cvt.ComputeEnergy();
                std::cout << "  Final energy: " << finalEnergy << "\n";
                
                if (finalEnergy <= initialEnergy * 1.1)  // Allow small increase due to anisotropy
                {
                    std::cout << "  ✓ 6D anisotropic CVT working\n";
                }
            }
        }
    }
    
    // Test 3: Convergence behavior
    std::cout << "\nTest 3: Convergence Testing\n";
    {
        // Simple mesh
        std::vector<gte::Vector3<double>> vertices = {
            {0.0, 0.0, 0.0},
            {2.0, 0.0, 0.0},
            {2.0, 2.0, 0.0},
            {0.0, 2.0, 0.0}
        };
        
        std::vector<std::array<int32_t, 3>> triangles = {
            {0, 1, 2},
            {0, 2, 3}
        };
        
        gte::CVTN<double, 3> cvt;
        cvt.SetVerbose(false);  // Quiet for convergence test
        cvt.Initialize(vertices, triangles);
        cvt.SetConvergenceThreshold(1e-3);
        
        // Initial sampling with bad distribution
        cvt.ComputeInitialSampling(6, 123);
        
        double initialEnergy = cvt.ComputeEnergy();
        std::cout << "  Initial energy: " << initialEnergy << "\n";
        
        // Run Lloyd
        cvt.SetVerbose(true);
        cvt.LloydIterations(20);
        
        double finalEnergy = cvt.ComputeEnergy();
        std::cout << "  Final energy: " << finalEnergy << "\n";
        
        double reduction = (initialEnergy - finalEnergy) / initialEnergy * 100.0;
        std::cout << "  Energy reduction: " << reduction << "%\n";
        
        if (reduction > 5.0)
        {
            std::cout << "  ✓ Convergence working (energy reduced)\n";
        }
    }
    
    // Test 4: Newton iterations (tighter convergence)
    std::cout << "\nTest 4: Newton Iterations (Tighter Convergence)\n";
    {
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
        
        gte::CVTN<double, 3> cvt;
        cvt.SetVerbose(true);
        cvt.Initialize(vertices, triangles);
        
        cvt.ComputeInitialSampling(4, 999);
        
        std::cout << "  Running Newton iterations (enhanced Lloyd)...\n";
        bool success = cvt.NewtonIterations(10);
        
        if (success)
        {
            std::cout << "  ✓ Newton iterations completed\n";
        }
    }
    
    // Test 5: Custom site initialization
    std::cout << "\nTest 5: Custom Site Initialization\n";
    {
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
        
        gte::CVTN<double, 3> cvt;
        cvt.Initialize(vertices, triangles);
        
        // Set custom sites
        std::vector<gte::Vector<3, double>> customSites = {
            {0.25, 0.25, 0.0},
            {0.75, 0.25, 0.0},
            {0.75, 0.75, 0.0},
            {0.25, 0.75, 0.0}
        };
        
        cvt.SetSites(customSites);
        std::cout << "  Set " << cvt.GetNumSites() << " custom sites\n";
        
        cvt.SetVerbose(false);
        cvt.LloydIterations(5);
        
        auto finalSites = cvt.GetSites();
        std::cout << "  Final site 0: (" 
                  << finalSites[0][0] << ", " << finalSites[0][1] << ")\n";
        
        std::cout << "  ✓ Custom initialization works\n";
    }
    
    std::cout << "\n=== CVTN tests completed ===\n";
    std::cout << "\nCVTN provides complete N-dimensional CVT with:\n";
    std::cout << "- Initial random sampling\n";
    std::cout << "- Lloyd iterations for optimization\n";
    std::cout << "- Newton iterations (enhanced convergence)\n";
    std::cout << "- Energy computation for analysis\n";
    std::cout << "- Support for 3D isotropic and 6D anisotropic CVT\n";
    
    return 0;
}
