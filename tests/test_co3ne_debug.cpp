#include <GTE/Mathematics/Co3Ne.h>
#include <iostream>
#include <vector>

using namespace gte;

int main()
{
    // Create a simple point cloud
    std::vector<Vector3<double>> points = {
        {0.0, 0.0, 0.0}, {1.0, 0.0, 0.0}, {1.0, 1.0, 0.0}, {0.0, 1.0, 0.0}
    };

    std::vector<Vector3<double>> outVertices;
    std::vector<std::array<int32_t, 3>> outTriangles;

    Co3Ne<double>::Parameters params;
    // kNeighbors=3: Minimum for PCA (need at least 3 points for covariance matrix)
    // This is intentionally minimal for debugging - production code should use 10-30
    params.kNeighbors = 3;

    std::cout << "Testing Co3Ne with " << points.size() << " points" << std::endl;
    bool success = Co3Ne<double>::Reconstruct(points, outVertices, outTriangles, params);

    std::cout << "Result: " << (success ? "SUCCESS" : "FAILED") << std::endl;
    std::cout << "Output: " << outTriangles.size() << " triangles" << std::endl;

    return 0;
}
