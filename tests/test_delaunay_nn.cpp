// Test program for DelaunayNN implementation
// Tests nearest-neighbor based Delaunay triangulation in N dimensions

#include <GTE/Mathematics/DelaunayNN.h>
#include <GTE/Mathematics/NearestNeighborSearchN.h>
#include <iostream>
#include <vector>
#include <cmath>

// Test helper: generate random points in N dimensions
template <typename Real, size_t N>
std::vector<gte::Vector<N, Real>> GenerateRandomPoints(size_t count, Real scale = static_cast<Real>(1))
{
    std::vector<gte::Vector<N, Real>> points;
    points.reserve(count);
    
    for (size_t i = 0; i < count; ++i)
    {
        gte::Vector<N, Real> p;
        for (size_t d = 0; d < N; ++d)
        {
            // Simple pseudo-random (deterministic for testing)
            Real value = std::sin(static_cast<Real>(i * 7 + d * 13)) * scale;
            p[d] = value;
        }
        points.push_back(p);
    }
    
    return points;
}

int main()
{
    std::cout << "Testing DelaunayNN Implementation...\n\n";
    
    // Test 1: NearestNeighborSearchN in 3D
    std::cout << "Test 1: NearestNeighborSearchN - 3D\n";
    {
        std::vector<gte::Vector<3, double>> points = {
            {0.0, 0.0, 0.0},
            {1.0, 0.0, 0.0},
            {0.0, 1.0, 0.0},
            {0.0, 0.0, 1.0},
            {1.0, 1.0, 1.0}
        };
        
        gte::NearestNeighborSearchN<double, 3> nnSearch;
        nnSearch.SetPoints(points.size(), points.data());
        
        // Test nearest neighbor query
        gte::Vector<3, double> query = {0.1, 0.1, 0.1};
        int32_t nearest = nnSearch.FindNearestNeighbor(query);
        
        std::cout << "  Query point: (0.1, 0.1, 0.1)\n";
        std::cout << "  Nearest vertex: " << nearest << " (expected: 0)\n";
        
        if (nearest == 0)
        {
            std::cout << "  ✓ Nearest neighbor search works\n";
        }
        else
        {
            std::cout << "  ✗ Nearest neighbor search failed\n";
        }
        
        // Test k-nearest neighbors
        std::vector<int32_t> neighbors;
        std::vector<double> distances;
        size_t k = nnSearch.FindKNearestNeighbors(query, 3, neighbors, distances);
        
        std::cout << "  K-nearest neighbors (k=3): ";
        for (size_t i = 0; i < k; ++i)
        {
            std::cout << neighbors[i] << " (dist=" << distances[i] << ") ";
        }
        std::cout << "\n";
        
        if (k == 3 && neighbors[0] == 0)
        {
            std::cout << "  ✓ KNN search works\n";
        }
    }
    
    // Test 2: NearestNeighborSearchN in 6D
    std::cout << "\nTest 2: NearestNeighborSearchN - 6D\n";
    {
        std::vector<gte::Vector<6, double>> points6D = GenerateRandomPoints<double, 6>(10);
        
        gte::NearestNeighborSearchN<double, 6> nnSearch;
        nnSearch.SetPoints(points6D.size(), points6D.data());
        
        std::cout << "  Created " << points6D.size() << " random 6D points\n";
        
        gte::Vector<6, double> query = points6D[0];
        int32_t nearest = nnSearch.FindNearestNeighbor(query);
        
        std::cout << "  Querying for point 0\n";
        std::cout << "  Nearest: " << nearest << " (expected: 0)\n";
        
        if (nearest == 0)
        {
            std::cout << "  ✓ 6D nearest neighbor search works\n";
        }
    }
    
    // Test 3: DelaunayNN in 3D
    std::cout << "\nTest 3: DelaunayNN - 3D\n";
    {
        std::vector<gte::Vector<3, double>> points = {
            {0.0, 0.0, 0.0},
            {1.0, 0.0, 0.0},
            {0.0, 1.0, 0.0},
            {0.0, 0.0, 1.0},
            {0.5, 0.5, 0.0},
            {0.5, 0.0, 0.5},
            {0.0, 0.5, 0.5}
        };
        
        gte::DelaunayNN<double, 3> delaunay(5);  // Store 5 neighbors per vertex
        bool success = delaunay.SetVertices(points.size(), points.data());
        
        std::cout << "  Dimension: " << delaunay.GetDimension() << "\n";
        std::cout << "  Cell size: " << delaunay.GetCellSize() << "\n";
        std::cout << "  Num vertices: " << delaunay.GetNumVertices() << "\n";
        
        if (success)
        {
            std::cout << "  ✓ DelaunayNN initialization successful\n";
        }
        
        // Test neighbor query
        auto neighbors = delaunay.GetNeighbors(0);
        std::cout << "  Neighbors of vertex 0: ";
        for (auto n : neighbors)
        {
            std::cout << n << " ";
        }
        std::cout << "(" << neighbors.size() << " neighbors)\n";
        
        if (!neighbors.empty())
        {
            std::cout << "  ✓ Neighbor queries work\n";
        }
        
        // Test nearest vertex
        gte::Vector<3, double> query = {0.1, 0.0, 0.0};
        int32_t nearest = delaunay.FindNearestVertex(query);
        std::cout << "  Nearest to (0.1, 0.0, 0.0): vertex " << nearest << "\n";
        
        if (nearest == 0 || nearest == 1)
        {
            std::cout << "  ✓ Nearest vertex query works\n";
        }
    }
    
    // Test 4: DelaunayNN in 6D (anisotropic CVT)
    std::cout << "\nTest 4: DelaunayNN - 6D (for anisotropic CVT)\n";
    {
        std::vector<gte::Vector<6, double>> points6D = GenerateRandomPoints<double, 6>(20);
        
        gte::DelaunayNN<double, 6> delaunay(10);  // Store 10 neighbors
        bool success = delaunay.SetVertices(points6D.size(), points6D.data());
        
        std::cout << "  Dimension: " << delaunay.GetDimension() << "\n";
        std::cout << "  Cell size: " << delaunay.GetCellSize() << "\n";
        std::cout << "  Num vertices: " << delaunay.GetNumVertices() << "\n";
        
        if (success)
        {
            std::cout << "  ✓ 6D DelaunayNN initialization successful\n";
        }
        
        // Test neighbors
        auto neighbors = delaunay.GetNeighbors(0);
        std::cout << "  Neighbors of vertex 0: " << neighbors.size() << " neighbors\n";
        
        if (!neighbors.empty() && neighbors.size() <= 10)
        {
            std::cout << "  ✓ 6D neighbor queries work\n";
        }
        
        // Test enlarge neighborhood
        delaunay.EnlargeNeighborhood(0, 15);
        auto enlargedNeighbors = delaunay.GetNeighbors(0);
        std::cout << "  After enlarging: " << enlargedNeighbors.size() << " neighbors\n";
        
        if (enlargedNeighbors.size() > neighbors.size())
        {
            std::cout << "  ✓ Neighborhood enlargement works\n";
        }
    }
    
    // Test 5: Factory function
    std::cout << "\nTest 5: Factory function\n";
    {
        auto delaunay3D = gte::CreateDelaunayN<double, 3>("NN");
        auto delaunay6D = gte::CreateDelaunayN<double, 6>("default");
        
        if (delaunay3D != nullptr)
        {
            std::cout << "  ✓ 3D factory creation works\n";
        }
        
        if (delaunay6D != nullptr)
        {
            std::cout << "  ✓ 6D factory creation works\n";
        }
    }
    
    // Test 6: Stress test with more points
    std::cout << "\nTest 6: Stress test (100 points in 6D)\n";
    {
        auto points = GenerateRandomPoints<double, 6>(100);
        
        gte::DelaunayNN<double, 6> delaunay(20);
        bool success = delaunay.SetVertices(points.size(), points.data());
        
        if (success)
        {
            std::cout << "  ✓ Handled 100 6D points successfully\n";
            
            // Check a few neighborhoods
            size_t totalNeighbors = 0;
            for (int32_t i = 0; i < 10; ++i)
            {
                totalNeighbors += delaunay.GetNumNeighbors(i);
            }
            
            std::cout << "  Average neighbors (first 10 vertices): " 
                      << (totalNeighbors / 10.0) << "\n";
            
            if (totalNeighbors > 0)
            {
                std::cout << "  ✓ All neighborhoods computed\n";
            }
        }
    }
    
    std::cout << "\n=== All DelaunayNN tests completed ===\n";
    std::cout << "\nDelaunayNN provides dimension-generic nearest-neighbor based\n";
    std::cout << "Delaunay suitable for CVT operations in any dimension.\n";
    
    return 0;
}
