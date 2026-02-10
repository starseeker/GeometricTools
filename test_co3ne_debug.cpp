#include <GTE/Mathematics/Co3NeFull.h>
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

    Co3NeFull<double>::Parameters params;
    params.kNeighbors = 3;

    std::cout << "Testing Co3NeFull with " << points.size() << " points" << std::endl;
    bool success = Co3NeFull<double>::Reconstruct(points, outVertices, outTriangles, params);

    std::cout << "Result: " << (success ? "SUCCESS" : "FAILED") << std::endl;
    std::cout << "Output: " << outTriangles.size() << " triangles" << std::endl;

    return 0;
}
