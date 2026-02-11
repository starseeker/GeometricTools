// Simple test for Co3Ne
#include <GTE/Mathematics/Co3NeFull.h>
#include <iostream>
#include <vector>

using namespace gte;

int main()
{
    // Create a simple cube point cloud
    std::vector<Vector3<double>> points = {
        {0,0,0}, {1,0,0}, {1,1,0}, {0,1,0},  // Bottom
        {0,0,1}, {1,0,1}, {1,1,1}, {0,1,1},  // Top
        {0.5,0.5,0}, {0.5,0.5,1},            // Centers
        {0,0.5,0.5}, {1,0.5,0.5},
        {0.5,0,0.5}, {0.5,1,0.5}
    };

    std::vector<Vector3<double>> vertices;
    std::vector<std::array<int32_t, 3>> triangles;

    Co3NeFull<double>::Parameters params;
    params.kNeighbors = 10;
    params.maxNormalAngle = 80.0;
    params.smoothWithRVD = false;  // Test without RVD first

    std::cout << "Testing Co3Ne with " << points.size() << " points..." << std::endl;
    
    if (Co3NeFull<double>::Reconstruct(points, vertices, triangles, params))
    {
        std::cout << "SUCCESS: Generated " << triangles.size() << " triangles" << std::endl;
    }
    else
    {
        std::cout << "FAILED: No triangles generated" << std::endl;
    }

    return 0;
}
