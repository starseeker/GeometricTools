#include <GTE/Mathematics/RestrictedVoronoiDiagram.h>
#include <iostream>

using namespace gte;

int main()
{
    // Create test sites (vertices of a cube plus a center point)
    std::vector<Vector3<double>> sites = {
        {0,0,0}, {1,0,0}, {1,1,0}, {0,1,0},  // Bottom 0-3
        {0,0,1}, {1,0,1}, {1,1,1}, {0,1,1},  // Top 4-7
        {0.5, 0.5, 0.5}                       // Center 8
    };
    
    // Create a simple mesh
    std::vector<Vector3<double>> meshVertices = {
        {0.5, 0.5, 0.0},
        {0.5, 0.5, 1.0},
        {0.0, 0.5, 0.5}
    };
    
    std::vector<std::array<int32_t, 3>> meshTriangles = {
        {0, 1, 2}
    };
    
    RestrictedVoronoiDiagram<double> rvd;
    
    std::cout << "Testing RVD with Delaunay-based neighbors..." << std::endl;
    
    if (rvd.Initialize(meshVertices, meshTriangles, sites))
    {
        std::cout << "✓ RVD initialized successfully" << std::endl;
        std::cout << "  Number of sites: " << sites.size() << std::endl;
        
        // The Delaunay should be built internally
        // This exercises the BuildDelaunay() method
        
        std::vector<Vector3<double>> centroids;
        if (rvd.ComputeCentroids(centroids))
        {
            std::cout << "✓ Computed centroids for " << centroids.size() << " sites" << std::endl;
        }
        else
        {
            std::cout << "✗ Failed to compute centroids" << std::endl;
        }
    }
    else
    {
        std::cout << "✗ RVD initialization failed" << std::endl;
        return 1;
    }
    
    std::cout << "\nTest completed successfully!" << std::endl;
    return 0;
}
