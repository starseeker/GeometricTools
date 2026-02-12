#include <GTE/Mathematics/Co3NeManifoldExtractor.h>
#include <GTE/Mathematics/Vector3.h>
#include <iostream>

using namespace gte;

int main()
{
    // Create simple cube points
    std::vector<Vector3<double>> points = {
        {0,0,0}, {1,0,0}, {1,1,0}, {0,1,0},  // Bottom
        {0,0,1}, {1,0,1}, {1,1,1}, {0,1,1}   // Top
    };
    
    // Create some good triangles (cube faces)
    std::vector<std::array<int32_t, 3>> goodTriangles = {
        {0,1,2}, {0,2,3},  // Bottom face
        {4,6,5}, {4,7,6}   // Top face
    };
    
    std::vector<std::array<int32_t, 3>> candidateTriangles = {
        {0,4,1}, {1,4,5},  // Front face
        {2,6,3}, {3,6,7}   // Back face
    };
    
    Co3NeManifoldExtractor<double> extractor(points);
    std::vector<std::array<int32_t, 3>> outTriangles;
    
    std::cout << "Testing manifold extractor..." << std::endl;
    std::cout << "Good triangles: " << goodTriangles.size() << std::endl;
    std::cout << "Candidate triangles: " << candidateTriangles.size() << std::endl;
    
    extractor.Extract(goodTriangles, candidateTriangles, outTriangles);
    
    std::cout << "Output triangles: " << outTriangles.size() << std::endl;
    
    for (size_t i = 0; i < outTriangles.size(); ++i)
    {
        std::cout << "Triangle " << i << ": " 
                  << outTriangles[i][0] << " "
                  << outTriangles[i][1] << " "
                  << outTriangles[i][2] << std::endl;
    }
    
    return 0;
}
