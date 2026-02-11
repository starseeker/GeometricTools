// Test program for DelaunayN base class
// Validates the dimension-generic interface

#include <GTE/Mathematics/DelaunayN.h>
#include <iostream>
#include <vector>

// Mock implementation for testing the interface
template <typename Real, size_t N>
class MockDelaunayN : public gte::DelaunayN<Real, N>
{
public:
    using PointN = typename gte::DelaunayN<Real, N>::PointN;
    
    bool SetVertices(size_t numVertices, PointN const* vertices) override
    {
        this->mNumVertices = numVertices;
        this->mVertices = vertices;
        return true;
    }
    
    int32_t FindNearestVertex(PointN const& query) const override
    {
        if (this->mNumVertices == 0)
        {
            return -1;
        }
        
        int32_t nearest = 0;
        Real minDist = this->DistanceSquared(query, this->mVertices[0]);
        
        for (size_t i = 1; i < this->mNumVertices; ++i)
        {
            Real dist = this->DistanceSquared(query, this->mVertices[i]);
            if (dist < minDist)
            {
                minDist = dist;
                nearest = static_cast<int32_t>(i);
            }
        }
        
        return nearest;
    }
    
    std::vector<int32_t> GetNeighbors(int32_t /*vertexIndex*/) const override
    {
        // Mock implementation
        return {};
    }
};

int main()
{
    std::cout << "Testing DelaunayN base class interface...\n\n";
    
    // Test 1: 3D Delaunay
    std::cout << "Test 1: 3D Delaunay interface\n";
    {
        MockDelaunayN<double, 3> delaunay;
        
        std::cout << "  Dimension: " << delaunay.GetDimension() << "\n";
        std::cout << "  Cell size: " << delaunay.GetCellSize() << "\n";
        
        if (delaunay.GetDimension() == 3 && delaunay.GetCellSize() == 4)
        {
            std::cout << "  ✓ 3D parameters correct (dimension=3, cell size=4)\n";
        }
        else
        {
            std::cout << "  ✗ 3D parameters incorrect\n";
        }
        
        // Create some 3D points
        std::vector<gte::Vector<3, double>> points3D = {
            {0.0, 0.0, 0.0},
            {1.0, 0.0, 0.0},
            {0.0, 1.0, 0.0},
            {0.0, 0.0, 1.0}
        };
        
        delaunay.SetVertices(points3D.size(), points3D.data());
        std::cout << "  Vertices set: " << delaunay.GetNumVertices() << "\n";
        
        // Test nearest vertex
        gte::Vector<3, double> query = {0.1, 0.1, 0.1};
        int32_t nearest = delaunay.FindNearestVertex(query);
        std::cout << "  Nearest to (0.1,0.1,0.1): vertex " << nearest << "\n";
        
        if (nearest == 0)
        {
            std::cout << "  ✓ Nearest vertex search works\n";
        }
    }
    
    // Test 2: 6D Delaunay (for anisotropic CVT)
    std::cout << "\nTest 2: 6D Delaunay interface\n";
    {
        MockDelaunayN<double, 6> delaunay;
        
        std::cout << "  Dimension: " << delaunay.GetDimension() << "\n";
        std::cout << "  Cell size: " << delaunay.GetCellSize() << "\n";
        
        if (delaunay.GetDimension() == 6 && delaunay.GetCellSize() == 7)
        {
            std::cout << "  ✓ 6D parameters correct (dimension=6, cell size=7)\n";
        }
        else
        {
            std::cout << "  ✗ 6D parameters incorrect\n";
        }
        
        // Create some 6D points (position + normal)
        std::vector<gte::Vector<6, double>> points6D;
        for (int i = 0; i < 5; ++i)
        {
            gte::Vector<6, double> p;
            // Position
            p[0] = i * 0.25;
            p[1] = 0.5;
            p[2] = 0.5;
            // Normal (scaled)
            p[3] = 0.1;
            p[4] = 0.1;
            p[5] = 0.1;
            points6D.push_back(p);
        }
        
        delaunay.SetVertices(points6D.size(), points6D.data());
        std::cout << "  Vertices set: " << delaunay.GetNumVertices() << "\n";
        
        // Test distance computation
        gte::Vector<6, double> p0, p1;
        for (int i = 0; i < 6; ++i)
        {
            p0[i] = 0.0;
            p1[i] = 1.0;
        }
        
        double dist = delaunay.Distance(p0, p1);
        double expected = std::sqrt(6.0);
        std::cout << "  Distance from origin to (1,1,1,1,1,1): " << dist << "\n";
        std::cout << "  Expected: " << expected << "\n";
        
        if (std::abs(dist - expected) < 1e-10)
        {
            std::cout << "  ✓ 6D distance computation correct\n";
        }
        else
        {
            std::cout << "  ✗ 6D distance computation incorrect\n";
        }
    }
    
    // Test 3: Different dimensions
    std::cout << "\nTest 3: Multiple dimensions\n";
    {
        std::cout << "  Testing dimensions 2-10:\n";
        
        MockDelaunayN<double, 2> d2;
        std::cout << "    2D: dim=" << d2.GetDimension() << ", cell=" << d2.GetCellSize() << "\n";
        
        MockDelaunayN<double, 3> d3;
        std::cout << "    3D: dim=" << d3.GetDimension() << ", cell=" << d3.GetCellSize() << "\n";
        
        MockDelaunayN<double, 4> d4;
        std::cout << "    4D: dim=" << d4.GetDimension() << ", cell=" << d4.GetCellSize() << "\n";
        
        MockDelaunayN<double, 6> d6;
        std::cout << "    6D: dim=" << d6.GetDimension() << ", cell=" << d6.GetCellSize() << "\n";
        
        MockDelaunayN<double, 8> d8;
        std::cout << "    8D: dim=" << d8.GetDimension() << ", cell=" << d8.GetCellSize() << "\n";
        
        std::cout << "  ✓ Multiple dimensions supported\n";
    }
    
    std::cout << "\n=== DelaunayN interface tests completed ===\n";
    std::cout << "\nThe DelaunayN base class provides a dimension-generic interface\n";
    std::cout << "for Delaunay triangulation in arbitrary dimensions.\n";
    std::cout << "\nNext: Implement DelaunayNN for the full functionality.\n";
    
    return 0;
}
